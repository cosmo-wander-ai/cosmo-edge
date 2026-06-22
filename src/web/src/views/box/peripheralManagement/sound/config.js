export const audioConfig = [
  {
    key: "audioDevPlayAudioFile",
    nameI18nKey: "boxOther.audioFileLabel",
    defaultValue: "",
    descriptionI18nKey: "boxOther.audioFileDesc",
    type: 'select',
  },
  {
    key: "audioDevVolume",
    nameI18nKey: "boxOther.volume",
    defaultValue: "50",
    descriptionI18nKey: "boxOther.volumeDesc",
    type: 'text',
    regexpr: "/^([1-9]|[1-9][0-9]|100)$/",
    failedTipI18nKey: "boxOther.volumeFailedTip"
  },
  {
    key: "audioDevDuration",
    nameI18nKey: "boxOther.playDurationSeconds",
    defaultValue: "60",
    descriptionI18nKey: "boxOther.playDurationDesc",
    type: 'text',
    regexpr: "/^([6-9][0-9]|[1-5][0-9][0-9]|600)$/",
    failedTipI18nKey: "boxOther.playDurationFailedTip"
  },
  {
    key: "audioDevPlayCount",
    nameI18nKey: "boxOther.playCount",
    defaultValue: "1",
    descriptionI18nKey: "boxOther.playCountDesc",
    type: 'text',
    regexpr: "/^([1-9]|[1-9][0-9]|100)$/",
    failedTipI18nKey: "boxOther.playCountFailedTip"
  },
  {
    key: "audioDevPlayInterval",
    nameI18nKey: "boxOther.playIntervalSeconds",
    defaultValue: "1",
    descriptionI18nKey: "boxOther.playIntervalDesc",
    type: 'text',
    regexpr: "/^([1-9]|[1-5][0-9]|60)$/",
    failedTipI18nKey: "boxOther.playIntervalFailedTip"
  }
]

export const soundConfig = [
  {
    key: "audioDevPlayString",
    nameI18nKey: "boxOther.playText",
    defaultValue: "",
    descriptionI18nKey: "boxOther.playTextDesc",
    type: 'text',
    regexpr: "/^(?=.{1,32}$).*/",
    failedTipI18nKey: "boxOther.playTextFailedTip"
  },
  {
    key: "audioDevPlayColor",
    value: "0",
    nameI18nKey: "boxOther.voice",
    defaultValue: "0",
    descriptionI18nKey: "boxOther.voiceDesc",
    type: 'select',
    options: [
      {
        labelI18nKey: "boxOther.maleVoice",
        value: "0"
      },
      {
        labelI18nKey: "boxOther.femaleVoice",
        value: "1"
      }
    ]
  },
  {
    key: "audioDevPlaySpeed",
    value: "50",
    nameI18nKey: "boxOther.speechRate",
    defaultValue: "50",
    descriptionI18nKey: "boxOther.speechRateDesc",
    type: 'text',
    regexpr: "/^([0-9]|[1-9][0-9]|100)$/",
    failedTipI18nKey: "boxOther.speechRateFailedTip"
  },
  {
    key: "audioDevVolume",
    nameI18nKey: "boxOther.volume",
    defaultValue: "50",
    descriptionI18nKey: "boxOther.volumeDesc",
    type: 'text',
    regexpr: "/^([1-9]|[1-9][0-9]|100)$/",
    failedTipI18nKey: "boxOther.volumeFailedTip"
  },
  {
    key: "audioDevDuration",
    nameI18nKey: "boxOther.playDurationSeconds",
    defaultValue: "60",
    descriptionI18nKey: "boxOther.playDurationDesc",
    type: 'text',
    regexpr: "/^([6-9][0-9]|[1-5][0-9][0-9]|600)$/",
    failedTipI18nKey: "boxOther.playDurationFailedTip"
  },
  {
    key: "audioDevPlayCount",
    nameI18nKey: "boxOther.playCount",
    defaultValue: "1",
    descriptionI18nKey: "boxOther.playCountDesc",
    type: 'text',
    regexpr: "/^([1-9]|[1-9][0-9]|100)$/",
    failedTipI18nKey: "boxOther.playCountFailedTip"
  },
  {
    key: "audioDevPlayInterval",
    nameI18nKey: "boxOther.playIntervalSeconds",
    defaultValue: "1",
    descriptionI18nKey: "boxOther.playIntervalDesc",
    type: 'text',
    regexpr: "/^([1-9]|[1-5][0-9]|60)$/",
    failedTipI18nKey: "boxOther.playIntervalFailedTip"
  }
]
