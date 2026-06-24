const socket = io();

// State
let devices = [];
let alerts = [];
let selectedDeviceSn = '';

// MQTT command examples and field help. The textarea only sends `body`; help text is rendered separately.
const commandDocs = {
    '/gtw/cwai/Camera/Add': {
        title: 'B01-通道创建',
        description: '创建实时流、点播、ONVIF、本地视频或 USB 通道。实时 RTSP 接入请使用 channelName、url、channelType。',
        body: {
            channelType: 0,
            channelName: '测试实时通道',
            url: 'rtsp://admin:a1234567@192.168.23.113:554/h264/ch1/main/av_stream',
            channelCode: '',
            channelPic: ''
        },
        fields: [
            ['channelType', 'number', '必填', '0:直播通道；1:点播通道；2:ONVIF；3:本地视频；6:USB'],
            ['channelName', 'string', '建议必填', '通道名称，页面展示用'],
            ['url', 'string', '实时流必填', '视频流地址。不要使用旧字段 rtspUrl'],
            ['channelCode', 'string', '非必填', '外部通道号，可为空'],
            ['channelPic', 'string', '非必填', '通道封面或抓图路径，可为空']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}],"resData":{"id":"RT0000000002"}}',
        tips: ['添加成功响应中的 resData.id 就是后续删除、更新、抓图、任务配置使用的通道 ID。']
    },
    '/gtw/cwai/Camera/Delete': {
        title: 'B02-通道删除',
        description: '删除单个视频通道。字段名必须是 videoChannelId，不是 cameraId。',
        body: { videoChannelId: 'RT0000000002' },
        fields: [['videoChannelId', 'string', '必填', '通道 ID，取自 Camera/Add 响应 resData.id 或 Camera/Page 列表']],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/Camera/Update': {
        title: 'B03-通道更新',
        description: '更新已有通道的名称、流地址和类型。',
        body: {
            videoChannelId: 'RT0000000002',
            channelType: 0,
            channelName: '测试实时通道-修改',
            url: 'rtsp://admin:a1234567@192.168.23.113:554/h264/ch1/main/av_stream',
            channelCode: 'CAM-001'
        },
        fields: [
            ['videoChannelId', 'string', '必填', '需要更新的通道 ID'],
            ['channelType', 'number', '必填', '通道类型，枚举同通道创建'],
            ['channelName', 'string', '非必填', '通道名称'],
            ['url', 'string', '非必填', '视频流地址'],
            ['channelCode', 'string', '非必填', '外部通道号']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/Camera/Page': {
        title: 'B04-通道分页查询',
        description: '分页查询视频通道列表。',
        body: { pageNum: 1, pageSize: 10, channelName: '测试', channelStatus: 1 },
        fields: [
            ['pageNum', 'number', '必填', '页码，从 1 开始'],
            ['pageSize', 'number', '必填', '每页数量'],
            ['channelName', 'string', '非必填', '按通道名称模糊查询'],
            ['channelStatus', 'number', '非必填', '通道状态过滤，按设备返回枚举使用']
        ],
        response: '{"resCode":1,"resData":{"total":1,"list":[{"videoChannelId":"RT0000000002","channelName":"测试实时通道"}]}}'
    },
    '/gtw/cwai/Camera/BatchDelete': {
        title: 'B05-通道批量删除',
        description: '批量删除多个视频通道。',
        body: { videoChannelIds: ['RT0000000002', 'RT0000000003'] },
        fields: [['videoChannelIds', 'string[]', '必填', '需要删除的通道 ID 数组']],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/Camera/GetPicture': {
        title: 'B06-通道抓图',
        description: '对指定通道进行抓图。',
        body: { videoChannelId: 'RT0000000002' },
        fields: [['videoChannelId', 'string', '必填', '需要抓图的通道 ID']],
        response: '{"resCode":1,"resData":{"picUrl":"/data/picture/RT0000000002.jpg"}}'
    },
    '/gtw/cwai/Camera/AddVideo': {
        title: 'B07-本地视频通道创建',
        description: '根据已上传到设备的本地视频文件创建通道。',
        body: {
            contentLength: '10485760',
            fileName: 'sample.mp4',
            filePath: '/data/video/sample.mp4',
            channelName: '离线视频'
        },
        fields: [
            ['contentLength', 'string', '必填', '视频文件大小，单位字节'],
            ['fileName', 'string', '必填', '视频文件名'],
            ['filePath', 'string', '必填', '设备上的视频文件路径'],
            ['channelName', 'string', '必填', '通道名称']
        ],
        response: '{"resCode":1,"resData":{"id":"VD0000000001"}}'
    },
    '/gtw/cwai/Task/SaveOrUpdate': {
        title: 'C01-保存任务配置并启用',
        description: '给指定通道保存算法任务配置，并启用或更新该任务。',
        body: {
            channelId: 'RT0000000002',
            algorithmId: 'ALG0000000001',
            scheduleId: '',
            taskConfig: {
                params: [{ key: 'alarmInterval', value: '60' }],
                areas: [],
                shieldedAreas: [],
                facesetConfig: []
            }
        },
        fields: [
            ['channelId', 'string', '必填', '通道 ID，取自通道创建或列表查询'],
            ['algorithmId', 'string', '必填', '算法 ID，取自算法列表或通道可配算法接口'],
            ['scheduleId', 'string', '非必填', '时间模板 ID，不使用模板时传空字符串或不传'],
            ['taskConfig', 'object', '必填', '算法运行配置，结构由算法元数据决定'],
            ['taskConfig.params', 'object[]', '非必填', '算法参数列表'],
            ['taskConfig.areas', 'object[]', '非必填', '检测区域列表'],
            ['taskConfig.shieldedAreas', 'object[]', '非必填', '屏蔽区域列表'],
            ['taskConfig.facesetConfig', 'object[]', '非必填', '人脸库相关配置']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/Task/ModifyParam': {
        title: 'C02-修改任务参数',
        description: '修改已配置任务的算法参数。',
        body: {
            channelId: 'RT0000000002',
            algorithmId: 'ALG0000000001',
            taskConfig: {
                params: [{ key: 'alarmInterval', value: '30' }],
                areas: [],
                shieldedAreas: [],
                facesetConfig: []
            }
        },
        fields: [
            ['channelId', 'string', '必填', '通道 ID'],
            ['algorithmId', 'string', '必填', '算法 ID'],
            ['taskConfig', 'object', '必填', '新的任务配置']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/Task/QueryParam': {
        title: 'C03-查询任务参数',
        description: '查询指定通道指定算法的当前任务配置。',
        body: { channelId: 'RT0000000002', algorithmId: 'ALG0000000001' },
        fields: [
            ['channelId', 'string', '必填', '通道 ID'],
            ['algorithmId', 'string', '必填', '算法 ID']
        ],
        response: '{"resCode":1,"resData":{"taskConfig":{"params":[],"areas":[],"shieldedAreas":[],"facesetConfig":[]}}}'
    },
    '/gtw/cwai/Task/SwitchTask': {
        title: 'C04-启停任务',
        description: '启用或停止指定通道上的指定算法任务。',
        body: { channelId: 'RT0000000002', algorithmId: 'ALG0000000001', switch: 1 },
        fields: [
            ['channelId', 'string', '必填', '通道 ID'],
            ['algorithmId', 'string', '必填', '算法 ID'],
            ['switch', 'number', '必填', '1 启用，0 停止']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/Task/Delete': {
        title: 'C05-删除任务配置',
        description: '删除指定通道和算法的任务配置。',
        body: { channelId: 'RT0000000002', algorithmId: 'ALG0000000001' },
        fields: [
            ['channelId', 'string', '必填', '通道 ID'],
            ['algorithmId', 'string', '必填', '算法 ID']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/Task/SelectAllAlgorithmInfo': {
        title: 'C06-查询通道可配算法',
        description: '查询指定通道可以配置的算法列表。',
        body: { channelId: 'RT0000000002' },
        fields: [['channelId', 'string', '必填', '通道 ID']],
        response: '{"resCode":1,"resData":[{"algorithmId":"ALG0000000001","algorithmName":"安全帽检测"}]}'
    },
    '/gtw/cwai/Task/SelectConfigByAlgorithmId': {
        title: 'C07-查询指定算法配置',
        description: '查询指定通道、指定算法的配置详情。',
        body: { channelId: 'RT0000000002', algorithmId: 'ALG0000000001' },
        fields: [
            ['channelId', 'string', '必填', '通道 ID'],
            ['algorithmId', 'string', '必填', '算法 ID']
        ],
        response: '{"resCode":1,"resData":{"algorithmId":"ALG0000000001","taskConfig":{"params":[]}}}'
    },
    '/v1/cwai/aihost/TaskCreate': {
        title: 'D01-创建核心视频任务',
        description: '向 AI 核心创建视频分析任务。',
        body: {
            taskId: 'task-RT0000000002-ALG0000000001',
            videoChannelId: 'RT0000000002',
            videoChannelName: '测试实时通道',
            streamUrl: 'rtsp://admin:a1234567@192.168.23.113:554/h264/ch1/main/av_stream',
            algorithmCode: 'helmet_detect',
            algorithmUpdateTime: '1716172800000',
            algorithmId: 'ALG0000000001',
            algorithmName: '安全帽检测',
            taskConfig: { params: [], areas: [], shieldedAreas: [], facesetConfig: [] }
        },
        fields: [
            ['taskId', 'string', '必填', '任务唯一 ID'],
            ['videoChannelId', 'string', '必填', '视频通道 ID'],
            ['videoChannelName', 'string', '非必填', '视频通道名称'],
            ['streamUrl', 'string', '非必填', '视频流地址'],
            ['algorithmCode', 'string', '必填', '算法编码'],
            ['algorithmUpdateTime', 'string', '必填', '算法更新时间或版本时间戳'],
            ['algorithmId', 'string', '非必填', '平台算法 ID'],
            ['algorithmName', 'string', '非必填', '算法名称'],
            ['taskConfig', 'object', '非必填', '算法运行配置']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/v1/cwai/aihost/TaskCancle': {
        title: 'D02-取消核心视频任务',
        description: '取消指定核心视频任务。',
        body: { taskId: 'task-RT0000000002-ALG0000000001' },
        fields: [['taskId', 'string', '必填', '需要取消的任务 ID']],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/v1/cwai/aihost/Info': {
        title: 'D03-查询核心信息',
        description: '查询 AI 核心运行信息。',
        body: { devId: 'CA16T012605060002' },
        fields: [['devId', 'string', '必填', '设备 SN 或设备 ID']],
        response: '{"resCode":1,"resData":{"devId":"CA16T012605060002","hostStatus":0}}'
    },
    '/gtw/cwai/System/QueryDeviceInfo': {
        title: 'E00-查询设备信息',
        description: '查询设备基础信息。',
        body: {},
        fields: [],
        response: '{"resCode":1,"resData":{}}'
    },
    '/gtw/cwai/System/QueryHardwareResource': {
        title: 'E00-查询硬件资源',
        description: '查询 CPU、内存、磁盘等硬件资源。',
        body: {},
        fields: [],
        response: '{"resCode":1,"resData":{}}'
    },
    '/gtw/cwai/System/QueryMqttAdapterParam': {
        title: 'E01-查询 MQTT 参数',
        description: '查询设备 MQTT 适配参数。',
        body: {},
        fields: [],
        response: '{"resCode":1,"resData":{"switch":true,"url":"192.168.0.10","port":1883,"authMode":0}}'
    },
    '/gtw/cwai/System/SetMqttAdapterParam': {
        title: 'E02-设置 MQTT 参数',
        description: '设置设备 MQTT 连接参数。',
        body: {
            switch: true,
            url: '192.168.0.10',
            port: 1883,
            authMode: 0,
            clientId: 'CA16T012605060002',
            userName: 'aibox::CA16T012605060002',
            passwd: '******'
        },
        fields: [
            ['switch', 'boolean', '必填', '是否启用 MQTT 适配'],
            ['url', 'string', '必填', 'MQTT Broker 地址'],
            ['port', 'number', '必填', 'MQTT Broker 端口'],
            ['authMode', 'number', '必填', '认证模式，按设备配置枚举填写'],
            ['clientId', 'string', '非必填', '客户端 ID'],
            ['userName', 'string', '非必填', 'MQTT 用户名'],
            ['passwd', 'string', '非必填', 'MQTT 密码']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/System/QueryRunModeParam': {
        title: 'E03-查询运行模式',
        description: '查询设备运行模式。',
        body: {},
        fields: [],
        response: '{"resCode":1,"resData":{"runMode":1}}'
    },
    '/gtw/cwai/System/ModifyRunModeParam': {
        title: 'E03-修改运行模式',
        description: '修改设备运行模式。',
        body: { runMode: 1 },
        fields: [['runMode', 'number', '必填', '运行模式值，按设备支持的枚举填写']],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/System/QueryIotNetworkParam': {
        title: 'E04-查询 IoT 网络参数',
        description: '查询 IoT 网络参数。',
        body: {},
        fields: [],
        response: '{"resCode":1,"resData":{}}'
    },
    '/gtw/cwai/System/ModifyIotNetworkParam': {
        title: 'E04-修改 IoT 网络参数',
        description: '修改 IoT 网络参数。',
        body: {
            mqttIp: '192.168.0.10',
            mqttPort: 1883,
            httpUrl: 'http://192.168.0.10:18080',
            status: true
        },
        fields: [
            ['mqttIp', 'string', '必填', 'MQTT 服务地址'],
            ['mqttPort', 'number', '必填', 'MQTT 服务端口'],
            ['httpUrl', 'string', '必填', 'HTTP 服务地址'],
            ['status', 'boolean', '必填', '是否启用该网络配置']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/Algorithm/Page': {
        title: 'F01-算法分页查询',
        description: '分页查询算法列表。',
        body: { pageNum: 1, pageSize: 10, algorithmName: '安全帽', algorithmId: '' },
        fields: [
            ['pageNum', 'number', '必填', '页码，从 1 开始'],
            ['pageSize', 'number', '必填', '每页数量'],
            ['algorithmName', 'string', '非必填', '算法名称过滤'],
            ['algorithmId', 'string', '非必填', '算法 ID 过滤']
        ],
        response: '{"resCode":1,"resData":{"total":1,"list":[{"algorithmId":"ALG0000000001","algorithmName":"安全帽检测"}]}}'
    },
    '/gtw/cwai/Algorithm/Add': {
        title: 'F02-添加算法配置',
        description: '新增算法配置记录。',
        body: {
            algorithmCode: 'helmet_detect',
            algorithmName: '安全帽检测',
            algorithmCategory: 1,
            algorithmUsage: 0,
            checkType: 0,
            remark: '工地安全检测',
            eventType: 'helmet_alarm',
            filePath: '/data/algorithm/helmet.zip'
        },
        fields: [
            ['algorithmCode', 'string', '非必填', '算法编码'],
            ['algorithmName', 'string', '必填', '算法名称'],
            ['algorithmCategory', 'number', '必填', '算法分类，按平台枚举填写'],
            ['algorithmUsage', 'number', '必填', '算法用途，按平台枚举填写'],
            ['checkType', 'number', '非必填', '检测类型'],
            ['remark', 'string', '非必填', '备注'],
            ['eventType', 'string', '非必填', '事件类型编码'],
            ['filePath', 'string', '非必填', '算法文件路径']
        ],
        response: '{"resCode":1,"resData":{"algorithmId":"ALG0000000001"}}'
    },
    '/gtw/cwai/Algorithm/Update': {
        title: 'F03-编辑算法配置',
        description: '编辑已有算法配置。',
        body: {
            algorithmId: 'ALG0000000001',
            algorithmName: '安全帽检测-新版',
            algorithmCategory: 1,
            remark: '更新参数'
        },
        fields: [
            ['algorithmId', 'string', '必填', '算法 ID'],
            ['algorithmName', 'string', '非必填', '算法名称'],
            ['algorithmCategory', 'number', '非必填', '算法分类'],
            ['remark', 'string', '非必填', '备注']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/Algorithm/Delete': {
        title: 'F04-删除算法配置',
        description: '删除指定算法配置。',
        body: { algorithmId: 'ALG0000000001' },
        fields: [['algorithmId', 'string', '必填', '算法 ID']],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/algorithm/layout/save': {
        title: 'F05-保存算法编排',
        description: '保存编排算法版本和元数据。',
        body: {
            confVersionId: 'CONF0000000001',
            configVersionName: '安全帽编排-v1',
            algorithmId: 'ALG0000000001',
            algorithmCategory: '1',
            algorithmUsage: '0',
            remark: '编排测试',
            atomicList: '[]',
            algorithmProcessdata: '{}',
            algorithmMetadata: '{}',
            filePath: '/data/algorithm/layout/helmet.json'
        },
        fields: [
            ['confVersionId', 'string', '必填', '编排配置版本 ID'],
            ['configVersionName', 'string', '非必填', '编排配置版本名称'],
            ['algorithmId', 'string', '必填', '算法 ID'],
            ['algorithmCategory', 'string', '非必填', '算法分类'],
            ['algorithmUsage', 'string', '非必填', '算法用途'],
            ['atomicList', 'string', '非必填', '当前 DTO 为字符串，传 JSON 字符串，例如 []'],
            ['algorithmProcessdata', 'string', '非必填', '当前 DTO 为字符串，传 JSON 字符串，例如 {}'],
            ['algorithmMetadata', 'string', '非必填', '当前 DTO 为字符串，传 JSON 字符串，例如 {}'],
            ['filePath', 'string', '非必填', '编排文件路径']
        ],
        response: '{"resCode":1,"resMsg":[{"msgCode":"0","msgText":"操作成功"}]}'
    },
    '/gtw/cwai/algorithm/layout/list': {
        title: 'F06-查询编排算法列表',
        description: '查询编排算法列表。',
        body: { supplier: 'CWAI', algorithmUsage: -1, filePath: '' },
        fields: [
            ['supplier', 'string', '非必填', '供应商标识'],
            ['algorithmUsage', 'number', '非必填', '-1 表示不过滤'],
            ['filePath', 'string', '非必填', '文件路径过滤']
        ],
        response: '{"resCode":1,"resData":[]}'
    },
    '/gtw/cwai/algorithm/layout/detail': {
        title: 'F07-查询编排算法详情',
        description: '查询指定编排算法详情。',
        body: { id: 'ALG0000000001', filePath: '/data/algorithm/layout/helmet.json' },
        fields: [
            ['id', 'string', '必填', '编排算法 ID'],
            ['filePath', 'string', '非必填', '编排文件路径']
        ],
        response: '{"resCode":1,"resData":{"id":"ALG0000000001"}}'
    },
    '/gtw/cwai/atomic/action/list': {
        title: 'F08-查询原子动作列表',
        description: '查询原子动作列表。',
        body: { actionUsage: 0, filePath: '' },
        fields: [
            ['actionUsage', 'number', '非必填', '动作用途，按平台枚举填写'],
            ['filePath', 'string', '非必填', '动作定义文件路径']
        ],
        response: '{"resCode":1,"resData":[]}'
    }
};

const templates = Object.fromEntries(
    Object.entries(commandDocs).map(([action, doc]) => [action, doc.body])
);

// DOM Elements
const tabs = document.querySelectorAll('.nav-btn');
const tabContents = document.querySelectorAll('.tab-content');
const alertsList = document.getElementById('alerts-list');
const deviceList = document.getElementById('device-list');
const mqttAction = document.getElementById('mqtt-action');
const customAction = document.getElementById('custom-action');
const mqttPayload = document.getElementById('mqtt-payload');
const mqttHelp = document.getElementById('mqtt-help');
const targetDevice = document.getElementById('target-device');
const sendMqttBtn = document.getElementById('send-mqtt');
const mqttLogs = document.getElementById('mqtt-logs');
const clearAlertsBtn = document.getElementById('clear-alerts');
const detailModal = document.getElementById('detail-modal');
const modalBody = document.getElementById('modal-body');
const closeModal = document.querySelector('.close-modal');

// --- Initialization ---

async function init() {
    try {
        const res = await fetch('/api/state');
        const data = await res.json();
        devices = data.devices;
        alerts = data.httpEvents;
        if (templates[mqttAction.value]) {
            mqttPayload.value = JSON.stringify(templates[mqttAction.value], null, 2);
        }
        renderCommandHelp(mqttAction.value);
        
        renderDevices();
        alerts.forEach(addAlertToUI);
        data.mqttMessages.reverse().forEach(addMqttLog);
    } catch (e) {
        console.error('Failed to init state', e);
    }
}

init();

// --- Tab Switching ---

tabs.forEach(tab => {
    tab.addEventListener('click', () => {
        const target = tab.dataset.tab;
        tabs.forEach(t => t.classList.remove('active'));
        tabContents.forEach(c => c.classList.remove('active'));
        tab.classList.add('active');
        document.getElementById(target).classList.add('active');
    });
});

// --- HTTP Alerts ---

function addAlertToUI(alert, prepend = false) {
    const card = document.createElement('div');
    card.className = 'alert-card';
    
    const time = new Date(alert._receivedAt || Date.now()).toLocaleTimeString();
    
    let imagesHtml = '';
    if (alert.orignalPicture) imagesHtml += `<div class="alert-img-container"><img src="data:image/jpeg;base64,${alert.orignalPicture}" onclick="showDetail('image', '${alert.orignalPicture}')"></div>`;
    if (alert.fullPicture) imagesHtml += `<div class="alert-img-container"><img src="data:image/jpeg;base64,${alert.fullPicture}" onclick="showDetail('image', '${alert.fullPicture}')"></div>`;
    if (alert.detectedPicture) imagesHtml += `<div class="alert-img-container"><img src="data:image/jpeg;base64,${alert.detectedPicture}" onclick="showDetail('image', '${alert.detectedPicture}')"></div>`;

    card.innerHTML = `
        <div class="alert-header">
            <span>${alert.algorithmName || '未知算法'}</span>
            <span>${time}</span>
        </div>
        <div class="alert-body">
            <div class="alert-images">${imagesHtml}</div>
            <div class="alert-info">
                <p><strong>设备 ID:</strong> ${alert.devId || 'N/A'}</p>
                <p><strong>通道:</strong> ${alert.channelName || alert.videoChannelId || 'N/A'}</p>
                <p><strong>类别:</strong> ${alert.category || 'N/A'}</p>
                ${alert.video ? `<button class="btn btn-primary btn-sm" onclick="showDetail('video', '${alert.video}')">播放视频</button>` : ''}
            </div>
        </div>
    `;
    
    if (prepend) {
        alertsList.prepend(card);
    } else {
        alertsList.appendChild(card);
    }
}

function showDetail(type, data) {
    modalBody.innerHTML = '';
    if (type === 'image') {
        const img = document.createElement('img');
        img.src = `data:image/jpeg;base64,${data}`;
        img.style.maxWidth = '100%';
        modalBody.appendChild(img);
    } else if (type === 'video') {
        const video = document.createElement('video');
        video.controls = true;
        video.autoplay = true;
        // The server serves /video/* paths
        video.src = `/video/${data.startsWith('/') ? data.substring(1) : data}`;
        modalBody.appendChild(video);
    }
    detailModal.classList.remove('hidden');
}

closeModal.onclick = () => detailModal.classList.add('hidden');
window.onclick = (event) => {
    if (event.target === detailModal) detailModal.classList.add('hidden');
};

clearAlertsBtn.onclick = () => {
    alertsList.innerHTML = '';
    alerts = [];
};

// --- MQTT Testing ---

function renderDevices() {
    deviceList.innerHTML = '';
    devices.forEach(dev => {
        const li = document.createElement('li');
        li.className = `device-item ${selectedDeviceSn === dev.deviceSn ? 'active' : ''}`;
        li.innerHTML = `
            <span class="device-sn">${dev.deviceSn}</span>
            <span class="device-status">${dev.status || 'online'}</span>
            <div style="font-size: 0.7rem; color: #64748b;">
                Model: ${dev.deviceModel || 'N/A'}<br>
                CPU: ${dev.cpuUsage ? (dev.cpuUsage * 100).toFixed(1) + '%' : 'N/A'}
            </div>
        `;
        li.onclick = () => {
            selectedDeviceSn = dev.deviceSn;
            targetDevice.value = dev.deviceSn;
            renderDevices();
        };
        deviceList.appendChild(li);
    });
}

mqttAction.onchange = () => {
    const val = mqttAction.value;
    if (val === 'custom') {
        customAction.classList.remove('hidden');
        renderCommandHelp(val);
    } else {
        customAction.classList.add('hidden');
        if (templates[val]) {
            mqttPayload.value = JSON.stringify(templates[val], null, 2);
        }
        renderCommandHelp(val);
    }
};

function escapeHtml(value) {
    return String(value)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

function renderCommandHelp(action) {
    if (!mqttHelp) return;

    const doc = commandDocs[action];
    if (!doc) {
        mqttHelp.innerHTML = `
            <div class="mqtt-help-title">自定义接口</div>
            <p>请输入完整 Action，并在 JSON Body 中填写该接口的业务 JSON。发送时工具会自动把业务 JSON 转成 MQTT 外层 body 字符串。</p>
        `;
        return;
    }

    const fieldRows = doc.fields.length
        ? doc.fields.map(([name, type, required, remark]) => `
            <tr>
                <td><code>${escapeHtml(name)}</code></td>
                <td>${escapeHtml(type)}</td>
                <td>${escapeHtml(required)}</td>
                <td>${escapeHtml(remark)}</td>
            </tr>
        `).join('')
        : '<tr><td colspan="4">无请求参数，发送空对象 <code>{}</code>。</td></tr>';

    const tips = (doc.tips || []).map(tip => `<li>${escapeHtml(tip)}</li>`).join('');
    const response = doc.response ? `<pre>${escapeHtml(doc.response)}</pre>` : '';

    mqttHelp.innerHTML = `
        <div class="mqtt-help-title">${escapeHtml(doc.title)}</div>
        <p>${escapeHtml(doc.description)}</p>
        ${tips ? `<ul>${tips}</ul>` : ''}
        <table>
            <thead>
                <tr><th>字段</th><th>类型</th><th>是否必须</th><th>说明</th></tr>
            </thead>
            <tbody>${fieldRows}</tbody>
        </table>
        <div class="mqtt-help-subtitle">响应示例</div>
        ${response}
        <p class="mqtt-help-note">注意：说明文字不会随指令发送，只有左侧 JSON Body 会作为业务参数发送。</p>
    `;
}

sendMqttBtn.onclick = async () => {
    const sn = targetDevice.value;
    const action = mqttAction.value === 'custom' ? customAction.value : mqttAction.value;
    let body;
    try {
        body = JSON.parse(mqttPayload.value);
    } catch (e) {
        alert('Invalid JSON payload');
        return;
    }

    if (!sn) {
        alert('Please specify a target device SN');
        return;
    }

    try {
        const res = await fetch('/api/mqtt/send', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ deviceSn: sn, action, body })
        });
        const result = await res.json();
        if (result.success) {
            console.log('Command sent');
        }
    } catch (e) {
        console.error('Failed to send MQTT', e);
    }
};

function addMqttLog(log) {
    const entry = document.createElement('div');
    entry.className = 'log-entry';
    const time = new Date(log.at).toLocaleTimeString();
    const typeClass = log.type === 'sent' ? 'log-type-sent' : 'log-type-received';
    const direction = log.type === 'sent' ? '-> SENT' : '<- RECV';
    
    entry.innerHTML = `
        <span class="${typeClass}">[${time}] ${direction}</span>
        <span style="color: #94a3b8">Topic: ${log.topic}</span>
        <pre style="margin: 5px 0 0 0; color: #e2e8f0">${JSON.stringify(log.payload, null, 2)}</pre>
    `;
    mqttLogs.prepend(entry);
}

// --- Socket.io Events ---

socket.on('http_event', (alert) => {
    alerts.unshift(alert);
    addAlertToUI(alert, true);
});

socket.on('mqtt_log', (log) => {
    addMqttLog(log);
});

socket.on('device_updated', (device) => {
    const idx = devices.findIndex(d => d.deviceSn === device.deviceSn);
    if (idx > -1) {
        devices[idx] = device;
    } else {
        devices.push(device);
    }
    renderDevices();
    refreshDeviceSnSelect();
});

socket.on('mqtt_client_connected', ({ id }) => {
    console.log('MQTT Client connected:', id);
});

socket.on('mqtt_client_disconnected', ({ id }) => {
    console.log('MQTT Client disconnected:', id);
});

// ══════════════════════════════════════════════════════════════════════════
// MQTT 自动化测试页面
// ══════════════════════════════════════════════════════════════════════════

let autoTestRunning = false;
let autoTestResults = [];
let autoTestCategories = [];

// 分类中文名映射
const categoryLabels = {
    builtin: '内置协议',
    'core-task': '核心任务',
    system: '系统管理',
    camera: '摄像机',
    task: '任务配置',
    algorithm: '算法管理',
    exception: '异常场景',
};

// 加载分类列表
async function loadCategories() {
    try {
        const res = await fetch('/api/mqtt/test/categories');
        autoTestCategories = await res.json();
        renderCategoryChecklist();
    } catch (e) {
        console.error('Failed to load categories', e);
        // 降级：使用默认分类
        autoTestCategories = Object.entries(categoryLabels).map(([key, label]) => ({
            key, label, icon: '', count: '?',
        }));
        renderCategoryChecklist();
    }
}

// 刷新目标设备下拉列表（从 state.devices 同步）
// 可以安全地在 Tab 未打开时调用（getElementById 返回 null 时直接返回）
function refreshDeviceSnSelect() {
    const select = document.getElementById('auto-test-sn');
    if (!select) return;  // Tab 未打开时不更新
    const currentVal = select.value;

    // 保留已有选项，重建列表
    select.innerHTML = '';

    // 始终保留模拟测试设备选项
    const syntheticOption = document.createElement('option');
    syntheticOption.value = 'test-device-auto-001';
    syntheticOption.textContent = '🖥️ 模拟测试设备 (test-device-auto-001)';
    syntheticOption.style.color = '#64748b';
    select.appendChild(syntheticOption);

    // 添加真实在线设备
    if (devices.length > 0) {
        const group = document.createElement('optgroup');
        group.label = '── 在线设备 ──';
        devices.forEach(dev => {
            const sn = dev.deviceSn || dev.devId || '';
            if (!sn) return;
            const option = document.createElement('option');
            option.value = sn;
            const model = dev.deviceModel ? ` (${dev.deviceModel})` : '';
            option.textContent = `${sn}${model}`;
            group.appendChild(option);
        });
        select.appendChild(group);
    } else {
        const emptyGroup = document.createElement('optgroup');
        emptyGroup.label = '── 在线设备 (暂无) ──';
        const emptyOption = document.createElement('option');
        emptyOption.disabled = true;
        emptyOption.textContent = '暂无在线设备';
        emptyGroup.appendChild(emptyOption);
        select.appendChild(emptyGroup);
    }

    // 恢复之前选中的值
    select.value = currentVal || 'test-device-auto-001';
}

function renderCategoryChecklist() {
    const container = document.getElementById('auto-test-categories');
    if (!container) return;
    container.innerHTML = autoTestCategories
        .filter(c => c.count > 0)
        .map(c => {
            const readOnly = c.count - c.mutationCount;
            const mutationNote = c.mutationCount > 0
                ? ` (<span style="color:#16a34a;">📖${readOnly}</span> + <span style="color:#dc2626;">🔧${c.mutationCount}</span>)`
                : '';
            return `
                <label title="${c.label}: 📖只读 ${readOnly}条, 🔧修改 ${c.mutationCount}条">
                    <input type="checkbox" value="${c.key}" checked>
                    <span>${c.icon} ${c.label}</span>
                    <span style="color:#64748b;font-size:0.75rem;">(${c.count})${mutationNote}</span>
                </label>
            `;
        }).join('');
}

// 设备 SN 切换时的处理
function onDeviceSnChange() {
    const select = document.getElementById('auto-test-sn');
    if (!select) return;
    const sn = select.value;
    const isSimulated = sn === 'test-device-auto-001';

    const warnBox = document.getElementById('auto-test-device-warning');
    if (warnBox) {
        if (isSimulated) {
            warnBox.classList.add('hidden');
        } else {
            warnBox.classList.remove('hidden');
            warnBox.querySelector('.warn-device-sn').textContent = escapeHtml(sn);
        }
    }

    // 真实设备时自动取消修改性用例分类的勾选
    if (!isSimulated) {
        const checkboxes = document.querySelectorAll('#auto-test-categories input[type="checkbox"]');
        checkboxes.forEach(cb => {
            const cat = autoTestCategories.find(c => c.key === cb.value);
            if (cat && cat.mutationCount > 0) {
                cb.checked = false;
                cb.parentElement.style.opacity = '0.5';
            }
        });
    } else {
        // 模拟设备恢复全部勾选
        const checkboxes = document.querySelectorAll('#auto-test-categories input[type="checkbox"]');
        checkboxes.forEach(cb => {
            cb.checked = true;
            cb.parentElement.style.opacity = '1';
        });
    }
}

// 获取选中的分类
function getSelectedCategories() {
    const checkboxes = document.querySelectorAll('#auto-test-categories input[type="checkbox"]:checked');
    return Array.from(checkboxes).map(cb => cb.value);
}

// 刷新历史报告列表
async function loadTestHistory() {
    const container = document.getElementById('auto-test-history');
    if (!container) return;
    try {
        const res = await fetch('/api/mqtt/test/history');
        const list = await res.json();
        if (list.length === 0) {
            container.innerHTML = '<p class="text-muted">暂无历史报告</p>';
            return;
        }
        container.innerHTML = list.map(item => {
            const s = item.summary || {};
            const time = item.timestamp ? new Date(item.timestamp).toLocaleString('zh-CN') : '未知';
            return `
                <div class="history-item" onclick="loadHistoryReport('${item.filename}')" title="点击查看详情">
                    <div class="history-item-time">${time}</div>
                    <div class="history-item-stats">
                        <span style="color:#16a34a;">✅ ${s.passed || 0}</span>
                        <span style="color:#dc2626;">❌ ${s.failed || 0}</span>
                        <span style="color:#d97706;">⏱ ${s.timeout || 0}</span>
                        <span style="color:#7c3aed;">⏱ ${s.totalElapsed || '?'}</span>
                        <span>通过率: ${s.passRate || '?'}</span>
                    </div>
                </div>
            `;
        }).join('');
    } catch (e) {
        container.innerHTML = '<p class="text-muted">加载失败</p>';
    }
}

// 点击历史报告查看详情
async function loadHistoryReport(filename) {
    try {
        const res = await fetch(`/api/mqtt/test/history/${encodeURIComponent(filename)}`);
        const report = await res.json();
        renderHistoryReport(report);
    } catch (e) {
        console.error('Failed to load report', e);
    }
}

function renderHistoryReport(report) {
    const container = document.getElementById('auto-test-results');
    const summary = report.summary || {};
    const results = report.results || [];

    // 更新汇总
    updateSummaryUI({
        summary: {
            total: results.length,
            passed: summary.passed || 0,
            failed: summary.failed || 0,
            timeout: summary.timeout || 0,
            warn: summary.warn || 0,
            passRate: summary.passRate || '0%',
            totalElapsed: summary.totalElapsed || '?',
            failures: summary.failures || [],
        },
    });

    // 渲染结果列表
    const statusEl = document.getElementById('auto-test-status');
    if (statusEl) statusEl.textContent = '(历史报告)';
    document.getElementById('auto-test-summary').classList.remove('hidden');
    document.getElementById('auto-test-progress-bar').classList.add('hidden');

    container.innerHTML = results.map((r, i) => buildResultLine(r, i)).join('');
    container.scrollTop = 0;
}

// 开始测试
async function startAutoTest() {
    if (autoTestRunning) return;

    const timeoutMs = parseInt(document.getElementById('auto-test-timeout').value) || 5000;
    const categories = getSelectedCategories();
    const deviceSn = document.getElementById('auto-test-sn').value || 'test-device-auto-001';

    if (categories.length === 0) {
        alert('请至少选择一个测试分类');
        return;
    }

    autoTestRunning = true;
    autoTestResults = [];
    updateButtonStates();

    // 清空
    const container = document.getElementById('auto-test-results');
    container.innerHTML = '<p class="text-muted">正在启动测试引擎...</p>';
    document.getElementById('auto-test-summary').classList.remove('hidden');
    document.getElementById('auto-test-progress-bar').classList.remove('hidden');
    const statusEl = document.getElementById('auto-test-status');
    if (statusEl) statusEl.textContent = '(启动中...)';

    try {
        const res = await fetch('/api/mqtt/test/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ categories, timeoutMs, deviceSn }),
        });
        const data = await res.json();
        if (!data.success) {
            alert('启动测试失败: ' + (data.error || '未知错误'));
            autoTestRunning = false;
            updateButtonStates();
        }
    } catch (e) {
        console.error('Failed to start test', e);
        autoTestRunning = false;
        updateButtonStates();
    }
}

// 停止测试
async function stopAutoTest() {
    if (!autoTestRunning) return;
    try {
        await fetch('/api/mqtt/test/stop', { method: 'POST' });
    } catch (e) {
        console.error('Failed to stop test', e);
    }
}

function updateButtonStates() {
    const startBtn = document.getElementById('auto-test-start');
    const stopBtn = document.getElementById('auto-test-stop');
    if (startBtn) startBtn.disabled = autoTestRunning;
    if (stopBtn) stopBtn.disabled = !autoTestRunning;
}

// 构建结果行 HTML（始终可展开查看输入输出）
function buildResultLine(r, index) {
    const catLabel = categoryLabels[r.category] || r.category;
    const mutationBadge = r.mutation ? ' 🔧' : ' 📖';
    let icon, cssClass;
    switch (r.verdict) {
        case 'PASS':  icon = '✅'; cssClass = 'test-result-pass'; break;
        case 'FAIL':  icon = '❌'; cssClass = 'test-result-fail'; break;
        case 'TIMEOUT': icon = '⏰'; cssClass = 'test-result-timeout'; break;
        case 'WARN':  icon = '⚠️'; cssClass = 'test-result-warn'; break;
        case 'ABORTED': icon = '⊘'; cssClass = 'test-result-info'; break;
        default:      icon = '⏳'; cssClass = 'test-result-running'; break;
    }
    const elapsed = r.elapsedMs ? `${r.elapsedMs}ms` : '';
    const errorLine = r.error ? `<div class="test-result-error">↳ ${escapeHtml(r.error)}</div>` : '';
    const detailLine = r.detail && !r.error ? `<div class="test-result-info">↳ ${escapeHtml(r.detail)}</div>` : '';

    const idx = index !== undefined ? index : Date.now();

    const reqPretty = r.requestPayload ? formatJsonForDisplay(r.requestPayload) : null;
    const rspPretty = r.responsePayload ? formatJsonForDisplay(r.responsePayload) : null;
    // 用 responseBody + responseHead 重建响应（兜底）
    const rspFromParts = (!rspPretty && r.responseHead)
        ? JSON.stringify({ head: r.responseHead, body: r.responseBody }, null, 2) : null;

    return `
        <div class="test-result-row">
            <div class="test-result-line clickable" onclick="togglePayload('payload-${idx}', this)">
                <span class="test-result-icon ${cssClass}">${icon}</span>
                <span class="test-result-text">
                    <span class="test-result-cat">${escapeHtml(catLabel)}</span>
                    ${escapeHtml(r.name)}
                    ${errorLine}
                    ${detailLine}
                </span>
                <span class="test-result-elapsed">${elapsed}</span>
                <span class="test-result-expand">▼</span>
            </div>
            <div class="test-payload-panel hidden" id="payload-${idx}">
                ${reqPretty ? `<div class="payload-section">
                    <div class="payload-label">📤 请求 (Request)</div>
                    <pre class="payload-json">${escapeHtml(reqPretty)}</pre>
                </div>` : `<div class="payload-section">
                    <div class="payload-label">📤 请求 (Request)</div>
                    <pre class="payload-json" style="color:#64748b;">(无请求数据)</pre>
                </div>`}
                ${rspPretty || rspFromParts ? `<div class="payload-section">
                    <div class="payload-label">📥 响应 (Response)</div>
                    <pre class="payload-json">${escapeHtml(rspPretty || rspFromParts)}</pre>
                </div>` : `<div class="payload-section">
                    <div class="payload-label">📥 响应 (Response)</div>
                    <pre class="payload-json" style="color:#64748b;">(无响应数据)</pre>
                </div>`}
            </div>
        </div>
    `;
}

// 格式化 JSON 用于展示（紧凑但可读）
function formatJsonForDisplay(jsonStr) {
    try {
        const obj = JSON.parse(jsonStr);
        return JSON.stringify(obj, null, 2);
    } catch (_) {
        return jsonStr;
    }
}

// 展开/收起输入输出面板
function togglePayload(payloadId, lineEl) {
    const panel = document.getElementById(payloadId);
    if (!panel) return;
    const isHidden = panel.classList.contains('hidden');
    panel.classList.toggle('hidden');
    // 更新展开箭头
    const expandEl = lineEl.querySelector('.test-result-expand');
    if (expandEl) {
        expandEl.textContent = isHidden ? '▲' : '▼';
    }
}

function updateSummaryUI(data) {
    const s = data.summary;
    document.getElementById('sum-pass').textContent = s.passed || 0;
    document.getElementById('sum-fail').textContent = s.failed || 0;
    document.getElementById('sum-timeout').textContent = s.timeout || 0;
    document.getElementById('sum-warn').textContent = s.warn || 0;
    document.getElementById('sum-rate').textContent = s.passRate || '0%';
    document.getElementById('sum-elapsed').textContent = s.totalElapsed || '0s';
}

// ── Socket.IO 事件 ──────────────────────────────────────────────────────

socket.on('test_start', (info) => {
    const container = document.getElementById('auto-test-results');
    container.innerHTML = '';
    const statusEl = document.getElementById('auto-test-status');
    if (statusEl) statusEl.textContent = `(0/${info.total})`;
    document.getElementById('auto-test-progress-bar').classList.remove('hidden');
    document.querySelector('.progress-bar-inner').style.width = '0%';
    document.querySelector('.progress-bar-text').textContent = `0 / ${info.total}`;
});

socket.on('test_progress', (info) => {
    const statusEl = document.getElementById('auto-test-status');
    if (statusEl) statusEl.textContent = `(${info.index}/${info.total})`;
    const pct = Math.round((info.index / info.total) * 100);
    document.querySelector('.progress-bar-inner').style.width = pct + '%';
    document.querySelector('.progress-bar-text').textContent = `${info.index} / ${info.total} (${pct}%)`;

    // 移除上一次的运行中占位行
    const container = document.getElementById('auto-test-results');
    const prevRunning = container.querySelector('.test-result-row-running');
    if (prevRunning) prevRunning.remove();

    // 添加新的"运行中"行（与结果行结构一致，方便 test_result 时移除）
    const catLabel = categoryLabels[info.category] || info.category;
    const row = document.createElement('div');
    row.className = 'test-result-row test-result-row-running';
    row.innerHTML = `
        <div class="test-result-line">
            <span class="test-result-icon test-result-running">⏳</span>
            <span class="test-result-text">
                <span class="test-result-cat">${escapeHtml(catLabel)}</span>
                ${escapeHtml(info.name)}
            </span>
            <span class="test-result-elapsed">...</span>
        </div>
    `;
    container.appendChild(row);
    container.scrollTop = container.scrollHeight;
});

socket.on('test_result', (result) => {
    // 调试日志：确认数据是否包含 payload 字段
    console.log('[test_result]', result.testId, result.verdict,
        'req:', typeof result.requestPayload, 'rsp:', typeof result.responsePayload,
        'head:', !!result.responseHead, 'body:', !!result.responseBody,
        'keys:', Object.keys(result).join(','));

    autoTestResults.push(result);

    const container = document.getElementById('auto-test-results');
    // 移除运行中占位行
    const runningRow = container.querySelector('.test-result-row-running');
    if (runningRow) runningRow.remove();

    // 插入结果行
    const tmpDiv = document.createElement('div');
    tmpDiv.innerHTML = buildResultLine(result, autoTestResults.length);
    const resultRow = tmpDiv.firstElementChild;
    if (resultRow) {
        container.appendChild(resultRow);
    }
    container.scrollTop = container.scrollHeight;

    // 实时更新汇总
    const counts = { PASS: 0, FAIL: 0, TIMEOUT: 0, WARN: 0, ABORTED: 0 };
    autoTestResults.forEach(r => { counts[r.verdict] = (counts[r.verdict] || 0) + 1; });
    const total = autoTestResults.length;
    const passRate = total > 0 ? ((counts.PASS / total) * 100).toFixed(1) + '%' : '0%';
    updateSummaryUI({
        summary: {
            passed: counts.PASS, failed: counts.FAIL, timeout: counts.TIMEOUT,
            warn: counts.WARN, aborted: counts.ABORTED, passRate, totalElapsed: '...',
            failures: [],
        },
    });
});

socket.on('test_done', (report) => {
    autoTestRunning = false;
    updateButtonStates();
    updateSummaryUI(report);

    const statusEl = document.getElementById('auto-test-status');
    if (statusEl) statusEl.textContent = '(完成)';
    document.querySelector('.progress-bar-text').textContent = '完成';

    // 刷新历史列表
    loadTestHistory();
});

socket.on('test_stopped', (info) => {
    autoTestRunning = false;
    updateButtonStates();
    const statusEl = document.getElementById('auto-test-status');
    if (statusEl) statusEl.textContent = '(已停止)';
    document.querySelector('.progress-bar-text').textContent = '已停止';
});

socket.on('test_error', (info) => {
    autoTestRunning = false;
    updateButtonStates();
    const statusEl = document.getElementById('auto-test-status');
    if (statusEl) statusEl.textContent = '(错误)';
});

socket.on('test_log', (entry) => {
    console.log(`[Test ${entry.level}] ${entry.msg}`);
});

// ── 页面初始化 ──────────────────────────────────────────────────────────

function initAutoTestPage() {
    loadCategories();
    loadTestHistory();
    refreshDeviceSnSelect();

    // 初始化时检查设备选择状态（首次默认是模拟设备，安全）
    setTimeout(() => onDeviceSnChange(), 100);

    document.getElementById('auto-test-start').onclick = startAutoTest;
    document.getElementById('auto-test-stop').onclick = stopAutoTest;

    document.getElementById('auto-test-select-all').onclick = () => {
        document.querySelectorAll('#auto-test-categories input[type="checkbox"]')
            .forEach(cb => { cb.checked = true; });
    };
    document.getElementById('auto-test-deselect-all').onclick = () => {
        document.querySelectorAll('#auto-test-categories input[type="checkbox"]')
            .forEach(cb => { cb.checked = false; });
    };
}

// 首次切换到自动化测试 Tab 时初始化
const autoTestTab = document.querySelector('[data-tab="mqtt-auto-test"]');
if (autoTestTab) {
    autoTestTab.addEventListener('click', () => {
        initAutoTestPage();
    }, { once: true });
}
