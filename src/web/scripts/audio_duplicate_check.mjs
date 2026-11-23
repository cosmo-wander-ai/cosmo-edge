import assert from 'node:assert/strict'
import { getCurrentInstance, h } from 'vue'
import { mountComponent } from './helpers/mount_behavior_component.mjs'

const empty = { default: { render: () => null } }
let result = 'duplicate'
let uploads = 0
let cancels = 0
const component = await mountComponent('views/box/audioManagement/audio/index.vue', {
  wrap: child => ({
    setup() {
      getCurrentInstance().appContext.app.component('el-table-column', { render: () => null })
      return () => h(child)
    }
  }),
  mocks: {
    '@/components/TopBar.vue': empty,
    '@element-plus/icons-vue': { WarningFilled: 'WarningFilled' },
    moment: { default: () => ({ format: () => '' }) },
    '@/utils/chunkUpload': {
      UploadPurpose: { AUDIO: 'audio' },
      uploadFileInChunks: async () => { uploads++; return { uploadId: 'upload-1' } }
    }
  },
  api: {
    queryAudioFile: async () => ({ resData: { audioFileList: [], totalCount: 0 } }),
    boxImportFile: async () => {
      if (result === 'ok') return { resCode: 1 }
      const key = result === 'duplicate' ? 'ExistedName' : 'FileMoveFailed'
      throw { resCode: 0, resMsg: [{ messageKey: `api.error.${key}`, msgText: key }] }
    },
    cancelAtomicModelUpload: async () => { cancels++ }
  }
})
const text = node => [node.text, ...node.children.map(text)].join(' ')
const button = label => component.all(n => n.type === 'el-button' && text(n).trim() === label)[0]
const alert = () => component.all(n => n.props.role === 'alert')[0]?.text
try {
  button('action.add').props.onClick()
  await component.settle()
  const choose = () => component.all(n => n.type === 'el-upload')[0].props['on-change']({ raw: { name: 'alert.wav', size: 10 } })
  choose()
  await component.settle()
  await button('action.save').props.onClick()
  await component.settle()
  assert.equal(alert(), 'boxOther.audioDuplicateName', 'duplicate explanation stays visible in the dialog')
  assert.equal(cancels, 1)
  result = 'fileError'
  choose()
  await component.settle()
  assert.equal(alert(), undefined, 'choosing another file clears the prior error')
  await button('action.save').props.onClick()
  await component.settle()
  assert.equal(alert(), 'api.error.FileMoveFailed', 'file errors must not be labeled duplicates')
  result = 'ok'
  choose()
  await component.settle()
  await button('action.save').props.onClick()
  await component.settle()
  assert.equal(alert(), undefined)
  assert.equal(uploads, 3)
} finally { component.unmount() }
console.log('Audio duplicate and file error checks passed')
