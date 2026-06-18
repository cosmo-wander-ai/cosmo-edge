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
});

socket.on('mqtt_client_connected', ({ id }) => {
    console.log('MQTT Client connected:', id);
});

socket.on('mqtt_client_disconnected', ({ id }) => {
    console.log('MQTT Client disconnected:', id);
});
