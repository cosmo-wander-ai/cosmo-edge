import moment from 'moment'

export const todayEventTimeRange = () => [
  moment().startOf('day').format('YYYY-MM-DD HH:mm:ss'),
  moment().endOf('day').format('YYYY-MM-DD HH:mm:ss')
]
