"""Strict NumPy/ctypes binding for the private resident FP32-I/O BMRT bridge."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import time
import numpy as np


def sha256(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()


def frozen_file(base,spec):
    path=(base/spec['path']).resolve()
    if not path.is_file() or sha256(path)!=spec['sha256']:
        raise ValueError('Missing or changed frozen artifact: '+path.name)
    return path


class Timing(C.Structure):
    _fields_=[(x,C.c_double) for x in ('h2d_ms','launch_sync_ms','d2h_ms','total_ms')]


class Bridge:
    def __init__(self,library,device=0):
        self.lib=C.CDLL(str(Path(library).resolve()));self.ctx=None;self.models=[]
        self.lib.lv_last_error.restype=C.c_char_p
        self.lib.lv_abi_version.restype=C.c_int
        if self.lib.lv_abi_version()!=1:raise RuntimeError('Unsupported bridge ABI')
        self.lib.lv_create.argtypes=[C.c_int];self.lib.lv_create.restype=C.c_void_p
        self.lib.lv_destroy.argtypes=[C.c_void_p];self.lib.lv_destroy.restype=None
        self.lib.lv_load.argtypes=[C.c_void_p,C.c_char_p];self.lib.lv_load.restype=C.c_int
        self.lib.lv_unload.argtypes=[C.c_void_p,C.c_int];self.lib.lv_unload.restype=C.c_int
        self.lib.lv_metadata.argtypes=[C.c_void_p,C.c_int,C.c_void_p,C.c_size_t];self.lib.lv_metadata.restype=C.c_int
        self.lib.lv_snapshot.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t];self.lib.lv_snapshot.restype=C.c_int
        self.lib.lv_run.argtypes=[C.c_void_p,C.c_int,C.POINTER(C.c_void_p),C.POINTER(C.c_size_t),C.c_size_t,
                                 C.POINTER(C.c_void_p),C.POINTER(C.c_size_t),C.c_size_t,C.POINTER(Timing)]
        self.lib.lv_run.restype=C.c_int
        self.ctx=self.lib.lv_create(device)
        if not self.ctx:raise RuntimeError(self.error())
    def error(self):
        return (self.lib.lv_last_error() or b'unknown bridge error').decode('utf-8',errors='replace')
    def _json(self,function,*args):
        if not self.ctx:raise RuntimeError('Bridge is closed')
        required=function(self.ctx,*args,None,0)
        if required<0:raise RuntimeError(self.error())
        if not 1<=required<=1024*1024:raise RuntimeError('Invalid metadata length')
        buf=C.create_string_buffer(max(8192,required*2))
        got=function(self.ctx,*args,buf,len(buf))
        if got<0:raise RuntimeError(self.error())
        return json.loads(buf.value.decode())
    def snapshot(self):return self._json(self.lib.lv_snapshot)
    def load(self,path,spec):
        if not self.ctx:raise RuntimeError('Bridge is closed')
        start=time.perf_counter();identifier=self.lib.lv_load(self.ctx,str(path).encode())
        if identifier<0:raise RuntimeError(self.error())
        wall_ms=(time.perf_counter()-start)*1000
        item=Model(self,identifier,spec)
        try:
            item.metadata=self._json(self.lib.lv_metadata,identifier)
            item.validate();item.metadata['python_load_call_ms']=wall_ms
        except BaseException:
            self.lib.lv_unload(self.ctx,identifier);raise
        self.models.append(item);return item
    def unload_all(self):
        errors=[]
        for item in reversed(self.models):
            if not item.closed:
                rc=self.lib.lv_unload(self.ctx,item.identifier);item.closed=True
                if rc:errors.append(self.error())
        if errors:raise RuntimeError('; '.join(errors))
    def close(self):
        if self.ctx:
            self.lib.lv_destroy(self.ctx);self.ctx=None
            for item in self.models:item.closed=True
    def __enter__(self):return self
    def __exit__(self,*args):self.close()


class Model:
    def __init__(self,bridge,identifier,spec):
        self.bridge=bridge;self.identifier=identifier;self.spec=spec;self.metadata={};self.closed=False
    def validate(self):
        if self.metadata['network']!=self.spec['network']:raise ValueError('Actual network name differs from manifest')
        for side in ('inputs','outputs'):
            expected=self.spec[side]
            actual=self.metadata[side]
            if len(actual)!=len(expected):raise ValueError('Actual '+side+' count differs from manifest')
            actual_names=[x['name'] for x in actual]
            expected_names=[x['name'] for x in expected.values()]
            if len(set(expected_names))!=len(expected_names) or set(actual_names)!=set(expected_names):
                raise ValueError('Actual '+side+' names differ from explicit alias map')
            for semantic,item in expected.items():
                observed=next(x for x in actual if x['name']==item['name'])
                if item['dtype']!='float32' or observed['dtype']!='float32' or observed['shape']!=item['shape']:
                    raise ValueError('Actual '+semantic+' shape/dtype differs from manifest')
                if observed['byte_size']!=int(np.prod(item['shape']))*4:raise ValueError('Unexpected tensor byte size')
    def run(self,feeds):
        if self.closed or not self.bridge.ctx:raise RuntimeError('Model/context is closed')
        if set(feeds)!=set(self.spec['inputs']):raise ValueError('Feed names differ from semantic input contract')
        incoming=[];outgoing=[];semantic_outputs=[]
        for item in self.metadata['inputs']:
            semantic=next(k for k,v in self.spec['inputs'].items() if v['name']==item['name'])
            value=feeds[semantic]
            if not isinstance(value,np.ndarray) or value.dtype!=np.float32 or list(value.shape)!=item['shape']:
                raise ValueError('Input '+semantic+' requires exact float32 shape; no implicit cast')
            if not np.isfinite(value).all():raise ValueError('Nonfinite input '+semantic)
            incoming.append(np.ascontiguousarray(value))
        for item in self.metadata['outputs']:
            semantic=next(k for k,v in self.spec['outputs'].items() if v['name']==item['name'])
            outgoing.append(np.empty(item['shape'],dtype=np.float32));semantic_outputs.append(semantic)
        pointers=lambda arrays:(C.c_void_p*len(arrays))(*(int(x.ctypes.data) for x in arrays))
        sizes=lambda arrays:(C.c_size_t*len(arrays))(*(x.nbytes for x in arrays))
        t=Timing();rc=self.bridge.lib.lv_run(self.bridge.ctx,self.identifier,pointers(incoming),sizes(incoming),len(incoming),
                                          pointers(outgoing),sizes(outgoing),len(outgoing),C.byref(t))
        if rc:raise RuntimeError(self.bridge.error())
        if not all(np.isfinite(x).all() for x in outgoing):raise RuntimeError('Nonfinite BMRT output; no result published')
        return dict(zip(semantic_outputs,outgoing)),{name:getattr(t,name) for name,_ in Timing._fields_}
