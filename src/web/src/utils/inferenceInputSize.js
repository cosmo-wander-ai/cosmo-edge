export const inputSizeValue = size => `${size.width}x${size.height}`

export const inputSizeOptions = model => (model?.inputSizes || []).map(size => ({
  value: inputSizeValue(size),
  label: `${size.width} × ${size.height}`,
  isDefault: model?.defaultInputSize && inputSizeValue(size) === inputSizeValue(model.defaultInputSize)
}))

export const defaultInputSizeValue = model => model?.defaultInputSize
  ? inputSizeValue(model.defaultInputSize)
  : ''

export const isInputSizeValid = (model, value) => !value || inputSizeOptions(model)
  .some(option => option.value === value)
