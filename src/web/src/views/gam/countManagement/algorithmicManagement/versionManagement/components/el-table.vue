<template>
  <div class="mv-wrap">
    <div class="mv-table-wrap">
      <div class="headtitle">
        <!-- 提示 -->
        <div class="hint">
          <div class="hint_content">
            <el-alert :title="t('validate.versionNoteOnlyOne')" type="info" :closable="false" show-icon></el-alert>
          </div>
        </div>

        <el-button type="primary" size="small" @click="addArithmetic = true" style="margin-bottom: 20px; float: right" class="mv-el-button">{{ t('action.uploadAlgorithmPackage') }}</el-button>
      </div>
      <!-- 表格数据 -->
      <el-table :data="tableData" tooltip-effect="dark" style="width: 100%" :header-cell-style="{ background: '#FAFAFA ' }" stripe>
        <el-table-column type="index" :label="t('field.no')" prop="date" width="100"></el-table-column>
        <el-table-column prop="algorithmName" :label="t('field.algorithmName')" min-width="100" show-overflow-tooltip></el-table-column>
        <el-table-column prop="versionNumber" :label="t('field.algorithmVersion')" min-width="100" show-overflow-tooltip></el-table-column>
        <el-table-column prop="sdkVersionNumber" :label="t('field.sdkVersion')" min-width="100" show-overflow-tooltip></el-table-column>
        <el-table-column prop="versionDesc" :label="t('field.description')" min-width="100" show-overflow-tooltip></el-table-column>
        <el-table-column prop="updateTime" :label="t('field.updateTime')" min-width="100" show-overflow-tooltip></el-table-column>
        <el-table-column :label="t('field.enabled')" min-width="100">
          <template slot-scope="scope">
            <el-switch :v-model="scope.row.versionStatus" class="switchStyle" :active-value="scope.row.versionStatus == 0" :inactive-value="scope.row.versionStatus == 1" :active-text="t('common.on')" :inactive-text="t('common.off')" active-color="#13ce66" inactive-color="#999999" @change="changeStatus(scope.row)"></el-switch>
          </template>
        </el-table-column>
        <el-table-column :label="t('field.actions')" min-width="81">
          <template slot-scope="scope">
            <div style="color: #409eff" v-if="scope.row.versionStatus == 0">
              <span @click="deleteOpen(scope.row)" style="cursor: pointer">{{ t('action.delete') }}</span>
            </div>
          </template>
        </el-table-column>
      </el-table>
      <!-- 添加算法模型 -->
      <el-dialog :title="t('action.uploadAlgorithmPackage')" :visible.sync="addArithmetic" width="500px" @close="close" center>
        <!-- <div style="margin-bottom: 20px">选择算法包文件：</div> -->
        <div style="display: flex">
          <div style="width: 90px; line-height: 40px; text-align: right">{{ t('field.selectFile') }}：</div>
          <el-input v-model.trim="algorithmModel" :placeholder="t('validate.selectTarGzFile')" :readonly="true">
            <template slot="append">
              <el-upload class="upload-demo" ref="upload" action="#" :limit="2" accept=".gz, .tar.gz" :on-change="handleChange" :file-list="offlineList" :before-upload="beforeUpload" :http-request="uploadFile" :show-file-list="false" :auto-upload="false">
                <el-button size="small" type="primary">{{ t('action.clickUpload') }}</el-button>
              </el-upload>
            </template>
          </el-input>
        </div>
        <div style="display: flex; margin-top: 20px">
          <div style="width: 90px; text-align: right">{{ t('field.description') }}：</div>
          <el-input type="textarea" :autosize="{ minRows: 3, maxRows: 6 }" :placeholder="t('validate.maxChars', { n: 100 })" v-model="explain" maxlength="100"></el-input>
        </div>

        <span slot="footer" class="dialog-footer">
          <el-button type="primary" @click="confirmAdd">{{ t('action.ok') }}</el-button>
          <el-button @click="addArithmetic = false">{{ t('action.cancel') }}</el-button>
        </span>
      </el-dialog>

      <!-- 删除 -->
      <el-dialog :title="t('common.notice')" :visible.sync="deleteVisible" center width="360px" height="157px">
        <div style="text-align: center">{{ t('action.confirmDelete') }}</div>
        <div slot="footer" class="dialog-footer">
          <el-button class="mv-el-button" size="small" type="primary" @click="deleteTask">{{ t('action.ok') }}</el-button>
          <el-button class size="small" @click="deleteVisible = false">{{ t('action.cancel') }}</el-button>
        </div>
      </el-dialog>
    </div>
  </div>
</template>

<script>
import { t } from '@/i18n'
export default {
  props: ['gpuCode'],
  setup() {
    return { t }
  },
  data() {
    return {
      algorithmModel: '',
      offlineList: [],
      explain: '',
      // 加载中
      loading: false,
      tableData: [],
      // 上传算法包
      addArithmetic: false,
      // 删除
      deleteVisible: false,
      versionId: ''
    }
  },
  created() {
    // this.init()
    console.log(this.$route.query.algorithmId)
    console.log(this.gpuCode)
  },
  mounted() { },
  methods: {
    init() {
      const params = {
        algorithmId: this.$route.query.algorithmId,
        gpuCode: this.gpuCode
      }
      this.$API
        .algorithmVersion(params)
        .then((res) => {
          const { resData } = res
          this.tableData = resData
        })
        .catch((err) => { })
    },
    // 赋值input
    handleChange(file, fileList) {
      // console.log(fileList);
      this.algorithmModel = file.name
      if (fileList.length > 0) {
        this.offlineList = [file]
      }
    },
    // 上传请求
    uploadFile(params) {
      console.log(this.gpuCode)
      // console.log(params);
      let formData = new FormData()
      formData.append('packageFile', params.file)
      formData.append('versionDesc', this.explain)
      formData.append('algorithmId', this.$route.query.algorithmId)
      formData.append('gpuCode', this.gpuCode)
      this.$API
        .algorithmAdd(formData)
        .then((res) => {
          let resData = res.resData
          if (resData) {
            this.$message.success(t('common.addSucceeded'))
            this.init()
          }
        })
        .catch((err) => { })
    },
    // 文件上传之前回调
    beforeUpload(file) {
      // console.log(file);
      var testmsg = file.name.substring(file.name.lastIndexOf('.') + 1)
      const extension2 = testmsg === 'gz'
      const extension5 = testmsg === 'tar.gz'
      const isLt2M = file.size / 1024 / 1024 < 300
      if (!extension2 && !extension5) {
        this.$message({
          message: t('validate.formatErrorTarGz'),
          type: 'warning'
        })
      }
      if (!isLt2M) {
        this.$message({
          message: t('validate.fileMaxSize', { n: 300 }),
          type: 'warning'
        })
      }
      return extension2 || (extension5 && isLt2M)
    },
    close() {
      this.algorithmModel = ''
      this.explain = ''
    },
    // 添加算法包确认
    confirmAdd() {
      this.$refs.upload.submit()
      this.addArithmetic = false
    },
    // 删除
    deleteOpen(row) {
      this.versionId = row.versionId
      this.deleteVisible = true
    },
    deleteTask() {
      this.$API
        .algorithmDelete({ versionId: this.versionId })
        .then((res) => {
          if (res.resData) {
            this.$message.success(t('common.deleteAlgorithmSucceeded'))
            this.init()
            return
          }
          this.$message.error(t('common.deleteAlgorithmSucceeded'))
        })
        .catch((err) => {
          console.log(err)
        })
      this.deleteVisible = false
    },
    // 提示关闭算法
    changeStatus(row) {
      let text = ''
      if (row.versionStatus == 1) {
        text = t('validate.confirmDisableVersion')
      } else {
        text = t('validate.confirmEnableVersion')
      }
      this.$confirm(`${text}`, t('common.notice'), {
        confirmButtonText: t('action.confirm'),
        cancelButtonText: t('action.cancel'),
        type: 'warning'
      })
        .then(() => {
          let message = ''
          if (row.versionStatus == 0) {
            message = t('common.algorithmEnabled')
            this.tableData.forEach((item) => {
              item.versionStatus = 0
            })
            row.versionStatus = 1
          } else {
            row.versionStatus = 0
            message = t('common.closedSucceeded')
          }
          const params = {
            versionId: row.versionId
          }
          this.$API
            .algorithmSwitch(params)
            .then((res) => {
              let resData = res.resData
              if (resData) {
                this.$message({
                  type: 'success',
                  message
                })
              }
            })
            .catch((err) => { })
        })
        .catch(() => {
          this.$message({
            type: 'info',
            message: t('common.operationCancelled')
          })
        })
    },
    // 表格居中
    headClass() {
      return 'text-align:center;font-weight: 700;'
    },
    cellClass() {
      return 'text-align:center'
    }
  },
  watch: {
    gpuCode: {
      handler(newVal, oldVal) {
        this.init()
      },
      immediate: true
    }
  }
}
</script>
<style scoped lang="scss">
/deep/ .switchStyle .el-switch__label {
  position: absolute;
  display: none;
  color: #fff;
}
/deep/ .switchStyle .el-switch__label--left {
  z-index: 9;
  left: 20px;
}
/deep/ .switchStyle .el-switch__label--right {
  z-index: 9;
  left: -3px;
}
/deep/ .switchStyle .el-switch__label.is-active {
  display: block;
}
.headtitle {
  display: flex;
  justify-content: space-between;
}
/deep/.dialog-footer button {
  width: 77px;
  height: 32px;
  border-radius: 2px;
  padding: 0px;
}
</style>
