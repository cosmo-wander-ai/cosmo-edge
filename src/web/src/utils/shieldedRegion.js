// Explicit switches take precedence; legacy layouts may only have headers.
export const isShieldedRegionEnabled = (metadata = {}) =>
  metadata.enableShieldedRegion ?? Boolean(metadata.shieldedRegion?.heads?.length)

export const getShieldedRegionHeads = (metadata = {}, t) => {
  if (!isShieldedRegionEnabled(metadata)) return undefined
  const heads = metadata.shieldedRegion?.heads
  if (Array.isArray(heads) && heads.length) return heads
  return [{
    key: 'name',
    name: t('glossary.regionName'),
    nameI18nKey: 'glossary.regionName',
    description: t('glossary.regionNameDesc'),
    descriptionI18nKey: 'glossary.regionNameDesc',
    failedTip: t('glossary.regionNameFailedTip'),
    failedTipI18nKey: 'glossary.regionNameFailedTip',
    defaultValue: '',
    isColumn: true,
    type: 'text',
    regexpr: '/^\\S{1,32}$/',
    options: null,
    range: '',
    step: null
  }]
}
