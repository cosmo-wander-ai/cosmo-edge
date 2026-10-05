"""Real Unix socket / process lifecycle checks with a test-only backend."""
import json
import multiprocessing
import os
from pathlib import Path
import signal
import socket
import struct
import sys
import tempfile
import time
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts/laya_runtime'))
from managed_worker import serve

class FakeBackend:
    identity = {'test_only': True}
    def __init__(self, *_): pass
    def infer(self, request, _):
        return {'request_id': request['request_id'], 'decision_status': 'unknown'}
    def close(self): pass

def run_worker(root):
    os.environ.pop('NOTIFY_SOCKET', None)
    serve('unused', 'unused', Path(root)/'run', Path(root)/'logs', FakeBackend)

def wait_ready(root, process):
    end=time.monotonic()+5
    while time.monotonic()<end:
        try:
            status=json.loads((Path(root)/'run/status.json').read_text())
            if status['state']=='ready' and status['pid']==process.pid: return
        except (FileNotFoundError, json.JSONDecodeError): pass
        if not process.is_alive(): raise AssertionError('worker failed to start')
        time.sleep(.02)
    raise AssertionError('worker readiness timeout')

class LifecycleTests(unittest.TestCase):
    def test_restart_duplicate_exclusion_and_protocol(self):
        with tempfile.TemporaryDirectory(prefix='laya-') as root:
            worker=multiprocessing.Process(target=run_worker,args=(root,));worker.start()
            try:
                wait_ready(root,worker)
                duplicate=multiprocessing.Process(target=run_worker,args=(root,));duplicate.start();duplicate.join(3)
                self.assertFalse(duplicate.is_alive());self.assertNotEqual(duplicate.exitcode,0)
                self.assertTrue(worker.is_alive())
                request={'protocol':'laya-shadow-v1','profile':'laya-256p-256s-v1','question_id':'helmet-review-en-v1',
                         'question_version':1,'request_id':'event-1','task_id':'task-1','run_epoch':'epoch-1',
                         'image_encoding':'jpeg','image_size':3,'image_width':4,'image_height':4}
                with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as client:
                    client.settimeout(2);client.connect(str(Path(root)/'run/worker.sock'))
                    raw=json.dumps(request).encode();client.sendall(struct.pack('!I',len(raw))+raw+b'jpg')
                    n=struct.unpack('!I',client.recv(4))[0];response=json.loads(client.recv(n))
                    self.assertEqual(response['request_id'],'event-1')
                os.kill(worker.pid,signal.SIGKILL);worker.join(3)
                self.assertTrue((Path(root)/'run/worker.sock').exists())
                worker=multiprocessing.Process(target=run_worker,args=(root,));worker.start();wait_ready(root,worker)
                worker.terminate();worker.join(3)
                self.assertEqual(worker.exitcode,0);self.assertFalse((Path(root)/'run/worker.sock').exists())
            finally:
                if worker.is_alive():worker.kill();worker.join()

    def test_reject_non_socket_path(self):
        with tempfile.TemporaryDirectory(prefix='laya-') as root:
            runtime=Path(root)/'run';runtime.mkdir();(runtime/'worker.sock').write_text('do not delete')
            worker=multiprocessing.Process(target=run_worker,args=(root,));worker.start();worker.join(3)
            self.assertFalse(worker.is_alive());self.assertNotEqual(worker.exitcode,0)
            self.assertEqual((runtime/'worker.sock').read_text(),'do not delete')

if __name__=='__main__': unittest.main()
