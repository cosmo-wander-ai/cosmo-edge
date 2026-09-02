<template>
  <section class="mv-content-wrap form-model" v-loading.fullscreen.lock="loading">
    <el-form label-position="right" size="small" :label-width="labelWidth ? labelWidth : defaultLabelWidth" class="demo-dynamic"
      @submit.prevent>
      <div class="for-border">
        <div v-for="(item, index) in paramz" :key="index">
          <span>{{ item.group ? item.group.name : '' }}</span>

          <!-- select选择框 -->
          <el-form-item v-if="item.type == 'select' && item.isColumn == true && showForm(item.senior)">
            <template #label>
              <span style="position:relative">
                <span>{{ resolveParamText(item, 'name') }}</span>
                <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top"
                  :disabled="item.description != null ? false : true">
                  <template #content>
                    <p>{{ resolveParamText(item, 'description') }}</p>
                  </template>
                  <el-icon>
                    <QuestionFilled />
                  </el-icon>
                </el-tooltip>
              </span>
            </template>
            <el-select @change="itemChange(item)" v-model="paramz[index].value">
              <el-option v-for="(select, idx) in item.options" :key="idx" :label="resolveParamOptionLabel(select, item)"
                :value="select.value"></el-option>
            </el-select>
          </el-form-item>

          <!-- switch 新加 -->
          <el-form-item
            v-if="item.type == 'switch' && item.isColumn == true && showLegacyParam(item) && showForm(item.senior)">
            <template #label>
              <span style="position:relative">
                <span>{{ resolveParamText(item, 'name') }}</span>
                <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top"
                  :disabled="item.description != null ? false : true">
                  <template #content>
                    <p>{{ resolveParamText(item, 'description') }}</p>
                  </template>
                  <el-icon>
                    <QuestionFilled />
                  </el-icon>
                </el-tooltip>
              </span>
            </template>
            <el-switch v-model="paramz[index].value" active-color="#13ce66" active-value="1" inactive-value="0"
              @change="switchEvent(item)"></el-switch>
          </el-form-item>

          <!-- checkbox组 -->
          <el-form-item v-if="item.type == 'check' && item.isColumn == true && showForm(item.senior)">
            <template #label>
              <span style="position:relative">
                <span>{{ resolveParamText(item, 'name') }}</span>
                <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                  <template #content>
                    <p>{{ resolveParamText(item, 'description') }}</p>
                  </template>
                  <el-icon>
                    <QuestionFilled />
                  </el-icon>
                </el-tooltip>
              </span>
            </template>
            <el-checkbox-group v-model="paramz[index].value" @change="itemChange(item)">
              <el-checkbox v-for="(check, idx) in item.options" :value="check.value" :key="idx">{{ resolveParamOptionLabel(check, item)
                }}</el-checkbox>
            </el-checkbox-group>
          </el-form-item>

          <!-- radio组 -->
          <el-form-item
            v-if="item.type == 'radio' && item.isColumn == true && showLegacyParam(item) && showForm(item.senior)">
            <template #label>
              <span style="position:relative">
                <span>{{ resolveParamText(item, 'name') }}</span>
                <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top"
                  :disabled="item.description != null ? false : true">
                  <template #content>
                    <p>{{ resolveParamText(item, 'description') }}</p>
                  </template>
                  <el-icon>
                    <QuestionFilled />
                  </el-icon>
                </el-tooltip>
              </span>
            </template>
            <el-radio-group v-model="paramz[index].value">
              <el-radio v-for="(radio, idx) in item.options" :value="radio.value" :key="idx">{{ resolveParamOptionLabel(radio, item) }}</el-radio>
            </el-radio-group>
          </el-form-item>

          <!-- slider组 -->
          <el-form-item v-if="item.type == 'slider' && item.isColumn == true && showForm(item.senior)">
            <template #label>
              <span style="position:relative">
                <span>{{ resolveParamText(item, 'name') }}</span>
                <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                  <template #content>
                    <p>{{ resolveParamText(item, 'description') }}</p>
                  </template>
                  <el-icon>
                    <QuestionFilled />
                  </el-icon>
                </el-tooltip>
              </span>
            </template>
            <el-slider v-model="paramz[index].value" :min="Number(item.range.split(',')[0])"
              :max="Number(item.range.split(',')[1])" style="width:210px" @change="handleSliderChange"></el-slider>
          </el-form-item>

          <!-- 文本域组 -->
          <el-form-item v-if="item.type == 'textarea' && item.isColumn == true && showForm(item.senior)">
            <template #label>
              <span style="position:relative">
                <span>{{ resolveParamText(item, 'name') }}</span>
                <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                  <template #content>
                    <p>{{ resolveParamText(item, 'description') }}</p>
                  </template>
                  <el-icon>
                    <QuestionFilled />
                  </el-icon>
                </el-tooltip>
              </span>
            </template>
            <el-input v-model.trim="paramz[index].value" style="width:210px;" type="textarea"
              :autosize="{ minRows: 2, maxRows: 4 }" maxlength="500" @input="handleInputChange"></el-input>
          </el-form-item>

          <!-- 输入框组 -->
          <el-form label-position="right" size="small" :model="item" :label-width="labelWidth ? labelWidth : defaultLabelWidth"
            @submit.prevent
            v-if="(item.type == 'number' || item.type == 'text') && showLegacyParam(item)">
            <el-form-item
              v-if="(item.type == 'number' || item.type == 'text') && item.isColumn == true && item.key !== 'LeadsRadio' && showForm(item.senior)"
              prop="value"
              :rules="[{ pattern: checkType(item.regexpr), message: resolveParamText(item, 'failedTip') || resolveParamText(item, 'description'), trigger: ['blur', 'change'] }]">
              <template #label>
                <span style="position:relative">
                  <span>{{ resolveParamText(item, 'name') }}</span>
                  <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                    <template #content>
                      <p>{{ resolveParamText(item, 'description') }}</p>
                    </template>
                    <el-icon>
                      <QuestionFilled />
                    </el-icon>
                  </el-tooltip>
                </span>
              </template>
              <el-input v-model.trim="paramz[index].value" type="text" @input="handleInputChange"></el-input>
            </el-form-item>

            <el-form-item
              v-if="(item.type == 'number' || item.type == 'text') && item.isColumn == true && item.key == 'LeadsRadio' && switchValue == '1' && showForm(item.senior)"
              prop="value"
              :rules="[{ pattern: checkType(item.regexpr), message: resolveParamText(item, 'failedTip') || resolveParamText(item, 'description'), trigger: ['blur', 'change'] }]">
              <template #label>
                <span style="position:relative">
                  <span>{{ resolveParamText(item, 'name') }}</span>
                  <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                    <template #content>
                      <p>{{ resolveParamText(item, 'description') }}</p>
                    </template>
                    <el-icon>
                      <QuestionFilled />
                    </el-icon>
                  </el-tooltip>
                </span>
              </template>
              <el-input v-model.trim="paramz[index].value" type="text" @input="handleInputChange"></el-input>
            </el-form-item>
          </el-form>

          <!-- 定制组件 confidenceConfig-->
          <el-form-item v-if="item.type == 'confidenceConfig' && item.isColumn == true && showForm(item.senior)">
            <template #label>
              <span style="position:relative">
                <span>{{ resolveParamText(item, 'name') }}</span>
                <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                  <template #content>
                    <p>{{ resolveParamText(item, 'description') }}</p>
                  </template>
                  <el-icon>
                    <QuestionFilled />
                  </el-icon>
                </el-tooltip>
              </span>
            </template>
            <div class="confidence-div">
              <el-select v-model="paramz[index].confidenceConfigValue1" class="confidence-select">
                <el-option v-for="obj in confidenceList" :key="obj.value" :label="obj.label" :value="obj.value"
                  size="mini"></el-option>
              </el-select>
              <div class="confidence-span">{{ t('action.fineTune') }}</div>
              <el-slider v-model="paramz[index].confidenceConfigValue2" class="confidence-slider" :marks="marks"
                show-input :min="-100" :max="100"></el-slider>
            </div>
          </el-form-item>

          <!-- 定制组件测距 distanceRate -->
          <el-form-item v-if="item.type == 'distanceRate' && item.isColumn == true && showForm(item.senior)">
            <template #label>
              <span style="position:relative">
                <span>{{ resolveParamText(item, 'name') }}</span>
                <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                  <template #content>
                    <p>{{ resolveParamText(item, 'description') }}</p>
                  </template>
                  <el-icon>
                    <QuestionFilled />
                  </el-icon>
                </el-tooltip>
              </span>
            </template>
            <div class="confidence-div">
              <el-input v-model.trim="paramz[index].value" type="text" @input="handleInputChange"></el-input>
              <el-button @click="openDistanceDialog(item)" type="primary" size="small">{{ t('action.measureDistance') }}</el-button>
            </div>
          </el-form-item>

          <!-- 工服库，机务库 -->
          <el-form-item
            v-if="(item.type == 'commoditySet' || item.type == 'workClothesSet') && item.isColumn == true && showForm(item.senior)">
            <template #label>
              <span style="position:relative">
                <span>{{ resolveParamText(item, 'name') }}</span>
                <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top"
                  :disabled="item.description != null ? false : true">
                  <template #content>
                    <p>{{ resolveParamText(item, 'description') }}</p>
                  </template>
                  <el-icon>
                    <QuestionFilled />
                  </el-icon>
                </el-tooltip>
              </span>
            </template>
            <el-select @change="itemChange(item)" v-model="paramz[index].value" filterable>
              <el-option v-for="(obj) in networkOptions" :key="obj.id" :label="obj.label"
                :value="obj.value"></el-option>
            </el-select>
          </el-form-item>

          <!-- 人脸库 -->
          <el-form-item v-if="item.type == 'faceSet' && item.isColumn == true && showForm(item.senior)">
            <template #label>
              <span style="position:relative">
                <span>{{ resolveParamText(item, 'name') }}</span>
                <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top"
                  :disabled="item.description != null ? false : true">
                  <template #content>
                    <p>{{ resolveParamText(item, 'description') }}</p>
                  </template>
                  <el-icon>
                    <QuestionFilled />
                  </el-icon>
                </el-tooltip>
              </span>
            </template>
            <el-transfer style="text-align: left;margin-bottom:20px;" v-model="transferList" :data="transferData"
              filterable target-order="push" :titles="[t('common.unselected'), t('common.selected')]" :button-texts="[t('action.moveLeft'), t('action.moveRight')]" :format="{
                noChecked: '${total}',
                hasChecked: '${checked}/${total}'
              }"></el-transfer>
          </el-form-item>

          <!-- 输入联动 -->
          <div
            v-if="(item.type == 'switch' || item.type == 'select') && showForm(item.senior) && item.children.length > 0">
            <div v-for="(el, childIdx) in item.children" :key="childIdx">
              <el-form v-if="showChildParam(item, el)" :disabled="disableChildParam(item, el)" label-position="right"
                size="small" :model="el" :label-width="labelWidth ? labelWidth : defaultLabelWidth" @submit.prevent>
                <!-- select  -->
                <el-form-item v-if="el.type == 'select' && el.isColumn == true && showForm(el.senior)">
                  <template #label>
                    <span style="position:relative" v-if="el.description">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top"
                        :disabled="el.description != null ? false : true">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <el-select @change="itemChange(el)" v-model="el.value">
                    <el-option v-for="(select, idx) in el.options" :key="idx" :label="resolveParamOptionLabel(select, el)"
                      :value="select.value"></el-option>
                  </el-select>
                </el-form-item>
                <!-- switch -->
                <el-form-item v-if="el.type == 'switch' && showForm(el.senior)">
                  <template #label>
                    <span style="position:relative">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top"
                        :disabled="el.description != null ? false : true">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <el-switch v-model="el.value" active-color="#13ce66" active-value="1" inactive-value="0"
                    @change="switchEvent(el)"></el-switch>
                </el-form-item>
                <!-- checkbox组 -->
                <el-form-item v-if="el.type == 'check' && el.isColumn == true && showForm(el.senior)">
                  <template #label>
                    <span style="position:relative">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <el-checkbox-group v-model="el.value" @change="itemChange(el)" :min="1">
                    <el-checkbox v-for="(check, idx) in el.options" :value="check.value" :key="idx">{{ resolveParamOptionLabel(check, el)
                      }}</el-checkbox>
                  </el-checkbox-group>
                </el-form-item>
                <!-- radio -->
                <el-form-item v-if="el.type == 'radio' && el.isColumn == true && showForm(el.senior)">
                  <template #label>
                    <span style="position:relative">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top"
                        :disabled="el.description != null ? false : true">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <el-radio-group v-model="el.value">
                    <el-radio v-for="(radio, idx) in el.options" :value="radio.value" :key="idx">{{ resolveParamOptionLabel(radio, el)
                      }}</el-radio>
                  </el-radio-group>
                </el-form-item>
                <!-- slider组 -->
                <el-form-item v-if="el.type == 'slider' && el.isColumn == true && showForm(el.senior)">
                  <template #label>
                    <span style="position:relative">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <el-slider v-model="el.value" :min="Number(item.range.split(',')[0])"
                    :max="Number(el.range.split(',')[1])" style="width:210px"></el-slider>
                </el-form-item>
                <!-- 文本域组 -->
                <el-form-item v-if="el.type == 'textarea' && el.isColumn == true && showForm(el.senior)">
                  <template #label>
                    <span style="position:relative">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <el-input v-model.trim="el.value" style="width:210px;" type="textarea"
                    :autosize="{ minRows: 2, maxRows: 4 }" maxlength="500"></el-input>
                </el-form-item>
                <!--  -->
                <el-form-item v-if="el.type == 'text' && showForm(el.senior)" prop="value"
                  :rules="[{ pattern: checkType(el.regexpr), message: resolveParamText(el, 'failedTip'), trigger: ['blur', 'change'] }]">
                  <template #label>
                    <span style="position:relative">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <el-input v-model.trim="el.value" type="text" @input="handleInputChange"></el-input>
                </el-form-item>
                <!-- confidenceConfig -->
                <el-form-item v-if="el.type == 'confidenceConfig' && el.isColumn == true && showForm(el.senior)">
                  <template #label>
                    <span style="position:relative">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <div class="confidence-div">
                    <el-select v-model="el.confidenceConfigValue1" class="confidence-select">
                      <el-option v-for="obj in confidenceList" :key="obj.value" :label="obj.label" :value="obj.value"
                        size="mini"></el-option>
                    </el-select>
                    <div class="confidence-span">{{ t('action.fineTune') }}</div>
                    <el-slider v-model="el.confidenceConfigValue2" class="confidence-slider" :marks="marks" show-input
                      :min="-100" :max="100"></el-slider>
                  </div>
                </el-form-item>
                <!-- 定制组件测距 distanceRate -->
                <el-form-item v-if="el.type == 'distanceRate' && el.isColumn == true && showForm(el.senior)">
                  <template #label>
                    <span style="position:relative">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <div class="confidence-div">
                    <el-input v-model.trim="el.value" type="text" @input="handleInputChange"></el-input>
                    <el-button @click="openDistanceDialog(el)" type="primary" size="small">{{ t('action.measureDistance') }}</el-button>
                  </div>
                </el-form-item>

                <!-- 工服库，机务库 -->
                <el-form-item
                  v-if="(el.type == 'commoditySet' || el.type == 'workClothesSet') && el.isColumn == true && showForm(el.senior)">
                  <template #label>
                    <span style="position:relative">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top"
                        :disabled="item.description != null ? false : true">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <el-select @change="itemChange(el)" v-model="el.value" filterable>
                    <el-option v-for="(obj) in networkOptions" :key="obj.id" :label="obj.label"
                      :value="obj.value"></el-option>
                  </el-select>
                </el-form-item>
                <!-- 人脸库 -->
                <el-form-item v-if="el.type == 'faceSet' && el.isColumn == true && showForm(el.senior)">
                  <template #label>
                    <span style="position:relative">
                      <span>{{ resolveParamText(el, 'name') }}</span>
                      <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top"
                        :disabled="el.description != null ? false : true">
                        <template #content>
                          <p>{{ resolveParamText(el, 'description') }}</p>
                        </template>
                        <el-icon>
                          <QuestionFilled />
                        </el-icon>
                      </el-tooltip>
                    </span>
                  </template>
                  <el-transfer style="text-align: left;margin-bottom:20px;" v-model="transferList" :data="transferData"
                    filterable target-order="push" :titles="[t('common.unselected'), t('common.selected')]" :button-texts="[t('action.moveLeft'), t('action.moveRight')]" :format="{
                      noChecked: '${total}',
                      hasChecked: '${checked}/${total}'
                    }"></el-transfer>
                </el-form-item>
              </el-form>
            </div>
          </div>

          <!-- 输入框组-一行 -->
          <el-form label-position="right" size="small" :model="item" :label-width="labelWidth ? labelWidth : defaultLabelWidth"
            @submit.prevent>
            <el-form-item v-if="item.type == 'initialPoint' && showForm(item.senior)" prop="value"
              :rules="[{ pattern: checkType(item.regexpr), message: resolveParamText(item, 'failedTip') || resolveParamText(item, 'description'), trigger: ['blur', 'change'] }]">
              <template #label>
                <span style="position:relative">
                  <span>{{ resolveParamText(item, 'name') }}</span>
                  <el-tooltip style="margin-left: 6px" class="item" effect="dark" placement="top">
                    <template #content>
                      <p>{{ resolveParamText(item, 'description') }}</p>
                    </template>
                    <el-icon>
                      <QuestionFilled />
                    </el-icon>
                  </el-tooltip>
                </span>
              </template>
              <el-input v-model.trim="paramz[index].value" type="text" @change="resolution"
                style="width:120px;"></el-input>
              <el-input style="margin-left:20px;width:120px;" v-model.trim="item.correlation.value" type="text"
                @change="resolution"></el-input>
            </el-form-item>
          </el-form>
        </div>
      </div>
    </el-form>
    <distance-dialog v-model:visible="distanceDialogVisible" :channelId="channelId" @confirm="handleDistanceConfirm" />
  </section>
</template>

<script setup>
import { ref, watch, computed, nextTick, getCurrentInstance } from 'vue'
import { t, currentLocale } from '@/i18n'
import {
  resolveResourceParamText,
  resolveResourceParamOptionLabel
} from '@/utils/i18nResource'
import { QuestionFilled } from '@element-plus/icons-vue'
import DistanceDialog from './distanceDialog.vue'
import defaultImage from '@/assets/CatchPhoto.png'
import { number } from 'echarts'
import {
  filterChannelEditableParams,
  filterTaskParamsForSubmission,
  flattenTaskParamTree,
  getParamDependencyCycleBreakIndexes,
  getParamDependencyKey,
  isLegacyUnrenderedParam,
  resolveChannelEditableFlags,
  serializeTaskParamTree
} from '@/utils/taskParamOwnership'

const { proxy } = getCurrentInstance()

const props = defineProps({
  algorithmCode: [String, Number],
  alarmType: String,
  formData: {
    type: Array,
    default: () => []
  },
  params: {
    type: Array,
    default: () => []
  },
  labelWidth: String,
  channelId: String,
  isHigher: Boolean,
  // Vue 3 modelValue support
  modelValue: {
    type: Array,
    default: () => []
  }
})

const emit = defineEmits(['modelVal', 'update:params', 'update:modelValue', 'itemChange', 'resetForm'])

const defaultLabelWidth = computed(() =>
  currentLocale.value === 'en-US' ? '350px' : '250px'
)

const resolveParamText = (item, field) =>
  resolveResourceParamText(item, props.algorithmCode, field)

const resolveParamOptionLabel = (option, parentItem) =>
  resolveResourceParamOptionLabel(option, parentItem, props.algorithmCode)

const loading = ref(false)
const rules = ref({})
const form = ref({})
const switchValue = ref('')
const paramz = ref([])
const realTime = ref(false)
const cut = ref(true)
const FaceSets = ref([])
const transferList = ref([])
const transferData = ref([])
const MachineryWarehouse = ref([])
const confidenceList = computed(() => [
  { label: t('glossary.strictThreshold'), value: 0 },
  { label: t('glossary.recommendedThreshold'), value: 1 }
])
const marks = computed(() => ({
  '-100': t('glossary.sensitive'),
  '100': t('glossary.strict')
}))
const networkOptions = ref([])
const distanceParamTarget = ref(null)
const distanceDialogVisible = ref(false)

const showForm = computed(() => {
  return (senior) => {
    const platformType = localStorage.getItem('platformType')
    const isPlatform = platformType == 1
    if (props.isHigher) return true
    if (senior == 2) return false
    if (isPlatform && senior == -1) return false
    if (!isPlatform && senior == 1) return false
    return true
  }
})

const getPlatformType = () => localStorage.getItem('platformType')

const showLegacyParam = (param) =>
  !isLegacyUnrenderedParam(param) ||
  String(getPlatformType() ?? '') !== '1'

const dependencyMatches = (parent, child) => {
  // Preserve the legacy form's loose matching for numeric/string switch values.
  // eslint-disable-next-line eqeqeq
  return child?.dependsOn?.value == parent?.value
}

const isEdgeChannelEditor = () => String(getPlatformType() ?? '') !== '1'
const showChildParam = (parent, child) =>
  isEdgeChannelEditor() || dependencyMatches(parent, child)
const disableChildParam = (parent, child) =>
  isEdgeChannelEditor() && !dependencyMatches(parent, child)

const getCurrentParams = () => {
  const currentParams =
    props.modelValue && props.modelValue.length > 0
      ? props.modelValue
      : props.params
  return filterChannelEditableParams(currentParams, getPlatformType())
}

const init = () => {
  let custId = window.localStorage.getItem('taskCustId')
    ? window.localStorage.getItem('taskCustId')
    : window.localStorage.getItem('currentCustId')

  const param = { name: '' }
  let platformType = localStorage.getItem('platformType')
  if (platformType == '1') {
    param.custId = custId ? custId : ''
  }

  // 使用modelValue或params，优先使用modelValue
  const currentParams = getCurrentParams()

  currentParams.forEach((item) => {
    if (item.type == 'commoditySet') {
      if (platformType == '15') {
        proxy.$API.boxQueryThingsLibInfo({ pageNum: 1, pageSize: 1000 }).then((res) => {
          const { resData } = res
          networkOptions.value = resData?.thingsLibList.map((obj) => ({
            label: obj.name,
            value: obj.id
          }))
          item.value = item.value ? item.value : networkOptions.value[0]?.value
        })
      } else {
        proxy.$API.machineMaterialGroupList(param).then((res) => {
          const { resData } = res
          networkOptions.value = resData.map((obj) => ({
            label: obj.name,
            value: obj.groupId
          }))
          item.value = item.value ? item.value : networkOptions.value[0]?.value
        })
      }
    } else if (item.type == 'workClothesSet') {
      if (platformType == '15') {
        proxy.$API.boxQueryPersonLibInfo({ pageNum: 1, pageSize: 1000 }).then((res) => {
          const { resData } = res
          networkOptions.value = resData?.personLibList.map((obj) => ({
            label: obj.name,
            value: obj.id
          }))
          item.value = item.value ? item.value : networkOptions.value[0]?.value
        })
      } else {
        proxy.$API.workClothesList(param).then((res) => {
          const { resData } = res
          networkOptions.value = resData.map((obj) => ({
            label: obj.name,
            value: obj.id
          }))
          item.value = item.value ? item.value : networkOptions.value[0]?.value
        })
      }
    } else if (item.type == 'faceSet') {
      if (platformType == '15') {
        proxy.$API.boxQueryFaceLibInfo({ pageNum: 1, pageSize: 1000 }).then((res) => {
          const { resData } = res
          FaceSets.value = resData?.faceLibList.map((obj) => ({
            faceSetName: obj.name,
            faceSetId: obj.id
          }))
          nextTick(() => {
            handleTransferData()
          })
        })
      } else {
        proxy.$API.queryFaceSetName(param).then((res) => {
          const { resData } = res
          FaceSets.value = resData
          nextTick(() => {
            handleTransferData()
          })
        })
      }
    }
  })
}

const handleTransferData = () => {
  transferData.value = []
  transferList.value = []

  // 使用modelValue或params，优先使用modelValue
  const currentParams = getCurrentParams()

  if (currentParams.length === 0) {
    FaceSets.value.forEach((element) => {
      transferData.value.push({
        key: String(element.faceSetId),
        label: element.faceSetName
      })
    })
  } else {
    let arr = []
    currentParams.forEach((element) => {
      if (element.type == 'faceSet') {
        arr = String(element.value || '')
          .split(',')
          .map((v) => v.trim())
          .filter(Boolean)
      }
    })
    FaceSets.value.forEach((obj) => {
      const id = String(obj.faceSetId)
      if (arr.includes(id)) {
        transferList.value.push(id)
      }
      transferData.value.push({
        key: id,
        label: obj.faceSetName
      })
    })
  }
}

const switchMethods = (data) => {
  let arr = []
  const editableKeys = new Set(data.map((item) => String(item.key)))
  const isPlatform = String(getPlatformType() ?? '') === '1'
  const cycleBreakIndexes = isPlatform
    ? new Set()
    : getParamDependencyCycleBreakIndexes(data)
  data.forEach((item, itemIndex) => {
    if (item.type == 'slider') {
      item.value = Number(item.value)
    }
    item.children = []
    data.forEach((el, childIndex) => {
      if (
        item.key == getParamDependencyKey(el) &&
        (isPlatform || !cycleBreakIndexes.has(childIndex))
      ) {
        item.children.push(el)
      }
    })
    const dependencyKey = getParamDependencyKey(item)
    if (
      dependencyKey === undefined ||
      (!isPlatform && cycleBreakIndexes.has(itemIndex)) ||
      (!isPlatform && !editableKeys.has(dependencyKey))
    ) {
      arr.push(item)
    }
  })

  // 标记为内部更新，避免触发watch
  isInternalUpdate.value = true
  paramz.value = arr

  // 延长重置标记的时间，确保所有相关更新完成
  nextTick(() => {
    setTimeout(() => {
      isInternalUpdate.value = false
    }, 200)
  })
}

const parseRegExpr = (rule) => {
  const r = (rule || '').toString().trim()
  if (!r) return /.*/
  if (r.startsWith('/') && r.lastIndexOf('/') > 0) {
    const last = r.lastIndexOf('/')
    const pattern = r.slice(1, last)
    const flags = r.slice(last + 1)
    try {
      return new RegExp(pattern, flags)
    } catch {
      return /.*/
    }
  }
  try {
    return new RegExp(r)
  } catch {
    return /.*/
  }
}
const checkType = (rule) => parseRegExpr(rule)

const buildParamPayload = (source = paramz.value) =>
  serializeTaskParamTree(source, { faceSetIds: transferList.value })

const emitParamUpdates = (
  source = paramz.value,
  { includeModelVal = true } = {}
) => {
  const payload = buildParamPayload(source)
  emit('update:modelValue', payload)
  emit('update:params', payload)
  if (includeModelVal) emit('modelVal', payload)
  return payload
}

const switchEvent = (item) => {
  if (item.key === 'isEnabled') {
    switchValue.value = flattenTaskParamTree(paramz.value).some(
      (param) => param.key === 'isEnabled' && String(param.value) === '1'
    )
      ? '1'
      : '0'
  }
  emitParamUpdates()
}

// 通用的数据更新函数
const triggerUpdate = () => {
  // 防抖处理
  clearTimeout(updateTimer)
  updateTimer = setTimeout(() => {
    // 移除频繁的console.log，只在必要时输出
    // console.log('dynamicForm触发更新:', buildParamPayload())
    emitParamUpdates()
  }, 100)
}

// 处理输入框变化
const handleInputChange = () => {
  emitParamUpdates()
}

// 处理滑块变化
const handleSliderChange = () => {
  emitParamUpdates()
}

const dependsOn = (newVal) => {
  const data = filterChannelEditableParams(
    JSON.parse(JSON.stringify(newVal || [])),
    getPlatformType()
  )
  for (var i = 0; i < data.length - 1; i++) {
    for (var j = i + 1; j < data.length; j++) {
      if (JSON.stringify(data[i].group) == JSON.stringify(data[j].group)) {
        delete data[j].group
      }
    }
  }
  let arr = []
  let initialPoint = {}
  let endPoint = {}
  data.forEach((item) => {
    if (item.type === 'confidenceConfig') {
      item.value = item.value ?? item.defaultValue ?? '0,0'
      const confidenceValues = String(item.value).split(',')
      const confidenceValue1 = Number(confidenceValues[0])
      const confidenceValue2 = Number(confidenceValues[1])
      item.confidenceConfigValue1 = Number.isFinite(confidenceValue1)
        ? confidenceValue1
        : 0
      item.confidenceConfigValue2 = Number.isFinite(confidenceValue2)
        ? confidenceValue2
        : 0
    }
    if (item.type == 'initialPoint') {
      initialPoint = item
    } else if (item.type == 'endPoint') {
      endPoint = item
    } else {
      // 确保所有字段都有值，优先使用 value，其次使用 defaultValue
      if (item.value === null || item.value === undefined) {
        item.value = item.defaultValue ?? ''
      }
      arr.push(item)
    }
  })
  switchValue.value = data.some(
    (item) => item.key === 'isEnabled' && String(item.value) === '1'
  )
    ? '1'
    : '0'
  if (initialPoint.key) {
    initialPoint.correlation = endPoint
    arr.push(initialPoint)
  }

  switchMethods(arr)
}

const resolution = () => {
  emitParamUpdates()
}

const submitForm = () => {
  // 优先使用当前的 paramz 数据，因为它包含了最新的表单状态
  const source =
    paramz.value && paramz.value.length > 0
      ? paramz.value
      : getCurrentParams()
  const currentParams = buildParamPayload(source)

  console.log('submitForm 验证的数据:', JSON.parse(JSON.stringify(currentParams)))

  if (validInput(currentParams)) {
    // 将最终的面板值同步一次，避免父层读取旧数据
    emitParamUpdates(currentParams)
    return true
  } else {
    return false
  }
}

const itemChange = (item) => {
  if (item.type == 'select') {
    if (item.key == 'RecognMode') {
      if (item.value == '0') {
        cut.value = true
      } else {
        cut.value = false
      }
    }
  }

  emitParamUpdates()
  emit('itemChange', item)
}

const resetForm = () => {
  let [data, newData] = [props.formData, []]
  data.forEach((group) => {
    group.elementList.forEach((param) => {
      newData.push({
        key: param.key,
        value: isNeedString(param) ? param.value.toString() : param.value
      })

      param.value = param.defaultValue
      if (param.type == 'check') {
        param.value = param.value.split(',')
      }
    })
  })
  emit('resetForm', newData)
}

const isNeedString = (param) => {
  return param.type == 'slider' || param.type == 'number' || param.type == 'text'
}

const validInput = (obj) => {
  console.log('validInput 接收到的数据:', JSON.parse(JSON.stringify(obj)))

  if (obj && obj.length) {
    let platformType = localStorage.getItem('platformType')
    const arr =
      platformType == 1
        ? obj
        : filterTaskParamsForSubmission(
          filterChannelEditableParams(obj, platformType)
        )

    console.log('过滤后的验证数据:', JSON.parse(JSON.stringify(arr)))

    for (let i = 0; i < arr.length; i++) {
      const param = arr[i]
      console.log(`验证字段 ${param.name}:`, param.value, '类型:', param.type)

      // 检查必填字段
      if (param.regexpr) {
        // 确保值不为空
        const valueToCheck = (param.value !== null && param.value !== undefined) ? param.value.toString() : ''

        let reg = parseRegExpr(param.regexpr)
        try {
          if (!reg.test(valueToCheck)) {
            console.error(`${param.name} 验证失败:`, valueToCheck, '正则:', param.regexpr)
            proxy.$message.error(t('validate.fieldInvalid', { field: param.name }))
            return false
          }
        } catch (e) {
          console.error('正则有误==>', e)
          proxy.$message.error(t('validate.regexInvalid', { field: param.name }))
          return false
        }
      }

    }
  }
  return true
}

const getAllFormData = () => {
  // 如果有 paramz 数据，返回当前的 paramz 值
  if (paramz.value && paramz.value.length > 0) {
    return buildParamPayload()
  }

  // 否则返回原来的逻辑
  let [data, newData] = [props.formData, []]
  data.forEach((group) => {
    group.elementList.forEach((param) => {
      newData.push({
        key: param.key,
        value: isNeedString(param) ? param.value.toString() : param.value
      })
    })
  })
  return buildParamPayload(newData)
}

const openDistanceDialog = (param) => {
  distanceParamTarget.value = param
  distanceDialogVisible.value = true
}

const handleDistanceConfirm = (val) => {
  if (distanceParamTarget.value) distanceParamTarget.value.value = val
  distanceParamTarget.value = null
  emitParamUpdates()
}

const buildStructureSignature = (arr) => {
  try {
    const list = Array.isArray(arr) ? arr : []
    const ownershipFlags =
      String(getPlatformType() ?? '') === '1'
        ? list.map(() => true)
        : resolveChannelEditableFlags(list)
    return JSON.stringify(
      list.map((i, index) => ({
        key: i.key,
        type: i.type,
        senior: i.senior ?? null,
        channelEditable: ownershipFlags[index],
        dependsOn: i.dependsOn ? { key: i.dependsOn.key, value: i.dependsOn.value } : null,
        optionsLen: Array.isArray(i.options) ? i.options.length : 0
      }))
    )
  } catch {
    return ''
  }
}

const inited = ref(false)
const modelSig = ref('')
const paramsSig = ref('')

const clearDynamicParams = () => {
  paramz.value = []
  transferList.value = []
  transferData.value = []
  switchValue.value = ''
  modelSig.value = ''
  paramsSig.value = ''
  inited.value = false
}

watch(transferList, () => {
  if (paramz.value && paramz.value.length > 0) {
    emitParamUpdates()
  }
}, { deep: true })

watch(
  [() => props.params, () => props.modelValue],
  () => {
    const currentParams = getCurrentParams()
    if (currentParams.length === 0) {
      clearDynamicParams()
      return
    }

    const nextSig = buildStructureSignature(currentParams)
    nextTick(() => {
      if (
        !inited.value ||
        nextSig !== paramsSig.value ||
        nextSig !== modelSig.value
      ) {
        init()
        paramsSig.value = nextSig
        modelSig.value = nextSig
        inited.value = true
      }
      dependsOn(currentParams)
    })
  },
  { immediate: true, deep: true }
)

// 监听paramz变化，但避免循环更新
watch(() => paramz.value, (newVal, oldVal) => {
  // 只有在非内部更新且数据真正发生变化时才触发更新
  if (newVal && newVal.length > 0 && !isInternalUpdate.value && oldVal && oldVal.length > 0) {
    // 深度比较，只有真正变化时才更新
    const hasChanged = JSON.stringify(newVal) !== JSON.stringify(oldVal)
    if (hasChanged) {
      // 立即同步到 modelValue，不使用防抖
      emitParamUpdates(newVal, { includeModelVal: false })
    }
  }
}, { deep: true })

let updateTimer = null
const isInternalUpdate = ref(false) // 标记是否为内部更新

defineExpose({
  submitForm,
  resetForm,
  getAllFormData
})
</script>

<style lang="scss" scoped>
.model-tit {
  font-size: 16px;
  line-height: 16px;
  color: #000;
  font-weight: normal;
  margin: 5px 0;
}

.for-border {
  border-radius: 10px;
}

.right-desc {
  position: absolute;
  left: 103%;
  top: 0;
}

.tips {
  margin-right: 5px;
}

.slider {
  width: 500px;
}

.btn-wrap {
  display: flex;
  justify-content: center;

  .el-button+.el-button {
    margin-left: 20px;
  }
}

.form-model {
  :deep(.el-input__inner) {
    width: 217px;
  }
}

.form-model :deep(.el-form-item__label) {
  text-align: right !important;
}

:deep(.el-input) {
  width: 210px;
}

:deep(.el-select) {
  width: 230px;
}

button {
  padding: 8px 18px;
}

.confidence-div {
  display: flex;
  align-items: center;
  gap: 12px;
}

.confidence-select {
  width: 230px;
  flex-shrink: 0;
}

.confidence-span {
  margin: 0;
  flex-shrink: 0;
  font-size: 14px;
  color: var(--el-text-color-regular);
  white-space: nowrap;
}

.confidence-slider {
  flex: 1;
  min-width: 300px;
  display: flex;
  align-items: center;

  :deep(.el-slider) {
    flex: 1;
    margin-right: 12px;
  }

  :deep(.el-slider__runway) {
    width: 100%;
    min-width: 180px;
  }

  :deep(.el-input) {
    width: 80px;
    flex-shrink: 0;
  }

  :deep(.el-input__inner) {
    width: 68px;
    text-align: center;
  }

  :deep(.el-slider__marks-text) {
    font-size: 12px;
    color: var(--el-text-color-placeholder);
  }

  :deep(.el-slider__marks-text:last-child) {
    width: 30px;
  }
}
</style>

<style>
.my-slider .el-form-item__content {
  width: 400px !important;
}

.form-model .el-form-item__label {
  text-align: right !important;
}

/* 确保所有表单项的label都右对齐 */
.demo-dynamic .el-form-item__label {
  text-align: right !important;
  justify-content: flex-end !important;
}

/* 修复可能的flex布局问题 */
.demo-dynamic .el-form-item .el-form-item__label {
  display: flex !important;
  align-items: center !important;
  justify-content: flex-end !important;
}
</style>
