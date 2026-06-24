'use strict';

/**
 * MQTT 自动化测试用例定义
 *
 * 每条用例包含：
 *   - id:       唯一标识
 *   - category: 分类 (builtin / core-task / system / camera / task / algorithm / exception)
 *   - action:   MQTT head.action 值
 *   - name:     中文名称
 *   - body:     业务 body 对象 (将序列化为 JSON 字符串作为 MQTT body 字段)
 *   - mutation: 是否为修改性操作 (true=会修改设备配置/数据, false/undefined=只读查询)
 *   - expect:   期望结果
 *       .resCode:    期望的 body.resCode (1=成功, 0=失败)
 *       .timeoutMs:  超时毫秒数 (默认 5000)
 *       .contains:   响应 body 中应包含的关键字段 (可选)
 *       .noReply:    设为 true 表示设备应该不回复 (用于异常场景)
 */

const TEST_DEVICE_SN = 'test-device-auto-001';

// ── 所有用例定义 ──────────────────────────────────────────────────────────

const testCases = [
    // ════════════════════════════════════════════════════════════════════
    // 内置协议 (2)
    // ════════════════════════════════════════════════════════════════════
    {
        id: 'builtin-01',
        category: 'builtin',
        action: '/d2p/aibox',
        name: '设备注册 (Register)',
        body: {
            devId: TEST_DEVICE_SN,
            supplier: 'CWAI',
            aiHostVersion: 'V1.5.0.0',
            engineType: 'sophon',
            deviceModel: 'BM1688',
            devType: 2,
        },
        isBuiltinRegister: true,  // 标记为注册协议，走特殊验证逻辑
        expect: {
            resCode: 1,
            timeoutMs: 5000,
        },
    },
    {
        id: 'builtin-02',
        category: 'builtin',
        action: '/d2p/aibox/heartbeat',
        name: '设备心跳 (Heartbeat)',
        body: {
            devId: TEST_DEVICE_SN,
            hostStatus: 0,
            customScore: 0.95,
            cpuUsage: 0.35,
            memTotal: 8192000,
            memAvailable: 4096000,
            gpuUsage: 0.20,
            gpuMemTotal: 4096000,
            gpuMemAvailable: 2048000,
            diskTotal: 64000000,
            diskAvailable: 32000000,
        },
        isBuiltinHeartbeat: true,  // 标记为心跳协议
        expect: {
            resCode: 1,
            timeoutMs: 5000,
        },
    },

    // ════════════════════════════════════════════════════════════════════
    // 核心任务 (3)
    // ════════════════════════════════════════════════════════════════════
    {
        id: 'core-01',
        category: 'core-task',
        action: '/v1/cwai/aihost/TaskCreate',
        mutation: true,
        name: '创建核心视频任务',
        body: {
            taskId: 'task-RT0000000002-ALG0000000001',
            videoChannelId: 'RT0000000002',
            videoChannelName: '测试实时通道',
            streamUrl: 'rtsp://admin:a1234567@192.168.23.113:554/h264/ch1/main/av_stream',
            algorithmCode: 'helmet_detect',
            algorithmUpdateTime: '1716172800000',
            algorithmId: 'ALG0000000001',
            algorithmName: '安全帽检测',
            taskConfig: { params: [], areas: [], shieldedAreas: [], facesetConfig: [] },
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'core-02',
        category: 'core-task',
        action: '/v1/cwai/aihost/TaskCancle',
        mutation: true,
        name: '取消核心视频任务',
        body: { taskId: 'task-RT0000000002-ALG0000000001' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'core-03',
        category: 'core-task',
        action: '/v1/cwai/aihost/Info',
        name: '查询核心信息',
        body: { devId: TEST_DEVICE_SN },
        expect: { resCode: 1, timeoutMs: 5000 },
    },

    // ════════════════════════════════════════════════════════════════════
    // 系统管理 (8)
    // ════════════════════════════════════════════════════════════════════
    {
        id: 'system-01',
        category: 'system',
        action: '/gtw/cwai/System/QueryDeviceInfo',
        name: '查询设备信息',
        body: {},
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'system-02',
        category: 'system',
        action: '/gtw/cwai/System/QueryHardwareResource',
        name: '查询硬件资源',
        body: {},
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'system-03',
        category: 'system',
        action: '/gtw/cwai/System/QueryMqttAdapterParam',
        name: '查询 MQTT 参数',
        body: {},
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'system-04',
        category: 'system',
        action: '/gtw/cwai/System/SetMqttAdapterParam',
        mutation: true,
        name: '设置 MQTT 参数',
        body: {
            switch: true,
            url: '192.168.0.10',
            port: 1883,
            authMode: 0,
            clientId: TEST_DEVICE_SN,
            userName: 'aibox::' + TEST_DEVICE_SN,
            passwd: '******',
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'system-05',
        category: 'system',
        action: '/gtw/cwai/System/QueryRunModeParam',
        name: '查询运行模式',
        body: {},
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'system-06',
        category: 'system',
        action: '/gtw/cwai/System/ModifyRunModeParam',
        mutation: true,
        name: '修改运行模式',
        body: { runMode: 1 },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'system-07',
        category: 'system',
        action: '/gtw/cwai/System/QueryIotNetworkParam',
        name: '查询 IoT 网络参数',
        body: {},
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'system-08',
        category: 'system',
        action: '/gtw/cwai/System/ModifyIotNetworkParam',
        mutation: true,
        name: '修改 IoT 网络参数',
        body: {
            mqttIp: '192.168.0.10',
            mqttPort: 1883,
            httpUrl: 'http://192.168.0.10:18080',
            status: true,
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },

    // ════════════════════════════════════════════════════════════════════
    // 摄像机 (7)
    // ════════════════════════════════════════════════════════════════════
    {
        id: 'camera-01',
        category: 'camera',
        action: '/gtw/cwai/Camera/Add',
        mutation: true,
        name: '添加通道 (实时流)',
        body: {
            channelType: 0,
            channelName: '测试实时通道',
            url: 'rtsp://admin:a1234567@192.168.23.113:554/h264/ch1/main/av_stream',
            channelCode: '',
            channelPic: '',
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'camera-02',
        category: 'camera',
        action: '/gtw/cwai/Camera/Delete',
        mutation: true,
        name: '删除通道',
        body: { videoChannelId: 'RT0000000002' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'camera-03',
        category: 'camera',
        action: '/gtw/cwai/Camera/Update',
        mutation: true,
        name: '更新通道',
        body: {
            videoChannelId: 'RT0000000002',
            channelType: 0,
            channelName: '测试实时通道-修改',
            url: 'rtsp://admin:a1234567@192.168.23.113:554/h264/ch1/main/av_stream',
            channelCode: 'CAM-001',
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'camera-04',
        category: 'camera',
        action: '/gtw/cwai/Camera/Page',
        name: '分页查询通道',
        body: { pageNum: 1, pageSize: 10, channelName: '', channelStatus: 1 },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'camera-05',
        category: 'camera',
        action: '/gtw/cwai/Camera/BatchDelete',
        mutation: true,
        name: '批量删除通道',
        body: { videoChannelIds: ['RT0000000002', 'RT0000000003'] },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'camera-06',
        category: 'camera',
        action: '/gtw/cwai/Camera/GetPicture',
        mutation: true,
        name: '通道抓图',
        body: { videoChannelId: 'RT0000000002' },
        expect: { resCode: 1, timeoutMs: 10000 },  // 抓图可能较慢
    },
    {
        id: 'camera-07',
        category: 'camera',
        action: '/gtw/cwai/Camera/AddVideo',
        mutation: true,
        name: '添加本地视频通道',
        body: {
            contentLength: '10485760',
            fileName: 'sample.mp4',
            filePath: '/data/video/sample.mp4',
            channelName: '离线视频',
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },

    // ════════════════════════════════════════════════════════════════════
    // 任务配置 (7)
    // ════════════════════════════════════════════════════════════════════
    {
        id: 'task-01',
        category: 'task',
        action: '/gtw/cwai/Task/SaveOrUpdate',
        mutation: true,
        name: '保存任务配置并启用',
        body: {
            channelId: 'RT0000000002',
            algorithmId: 'ALG0000000001',
            scheduleId: '',
            taskConfig: {
                params: [{ key: 'alarmInterval', value: '60' }],
                areas: [],
                shieldedAreas: [],
                facesetConfig: [],
            },
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'task-02',
        category: 'task',
        action: '/gtw/cwai/Task/ModifyParam',
        mutation: true,
        name: '修改任务参数',
        body: {
            channelId: 'RT0000000002',
            algorithmId: 'ALG0000000001',
            taskConfig: {
                params: [{ key: 'alarmInterval', value: '30' }],
                areas: [],
                shieldedAreas: [],
                facesetConfig: [],
            },
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'task-03',
        category: 'task',
        action: '/gtw/cwai/Task/QueryParam',
        name: '查询任务参数',
        body: { channelId: 'RT0000000002', algorithmId: 'ALG0000000001' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'task-04',
        category: 'task',
        action: '/gtw/cwai/Task/SwitchTask',
        mutation: true,
        name: '启停任务',
        body: { channelId: 'RT0000000002', algorithmId: 'ALG0000000001', switch: 1 },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'task-05',
        category: 'task',
        action: '/gtw/cwai/Task/Delete',
        mutation: true,
        name: '删除任务配置',
        body: { channelId: 'RT0000000002', algorithmId: 'ALG0000000001' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'task-06',
        category: 'task',
        action: '/gtw/cwai/Task/SelectAllAlgorithmInfo',
        name: '查询通道可配算法',
        body: { channelId: 'RT0000000002' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'task-07',
        category: 'task',
        action: '/gtw/cwai/Task/SelectConfigByAlgorithmId',
        name: '查询指定算法配置',
        body: { channelId: 'RT0000000002', algorithmId: 'ALG0000000001' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },

    // ════════════════════════════════════════════════════════════════════
    // 算法 (8)
    // ════════════════════════════════════════════════════════════════════
    {
        id: 'algo-01',
        category: 'algorithm',
        action: '/gtw/cwai/Algorithm/Page',
        name: '算法分页查询',
        body: { pageNum: 1, pageSize: 10, algorithmName: '', algorithmId: '' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'algo-02',
        category: 'algorithm',
        action: '/gtw/cwai/Algorithm/Add',
        mutation: true,
        name: '添加算法配置',
        body: {
            algorithmCode: 'helmet_detect',
            algorithmName: '安全帽检测',
            algorithmCategory: 1,
            algorithmUsage: 0,
            checkType: 0,
            remark: '工地安全检测',
            eventType: 'helmet_alarm',
            filePath: '/data/algorithm/helmet.zip',
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'algo-03',
        category: 'algorithm',
        action: '/gtw/cwai/Algorithm/Update',
        mutation: true,
        name: '编辑算法配置',
        body: {
            algorithmId: 'ALG0000000001',
            algorithmName: '安全帽检测-新版',
            algorithmCategory: 1,
            remark: '更新参数',
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'algo-04',
        category: 'algorithm',
        action: '/gtw/cwai/Algorithm/Delete',
        mutation: true,
        name: '删除算法配置',
        body: { algorithmId: 'ALG0000000001' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'algo-05',
        category: 'algorithm',
        action: '/gtw/cwai/algorithm/layout/save',
        mutation: true,
        name: '保存算法编排',
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
            filePath: '/data/algorithm/layout/helmet.json',
        },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'algo-06',
        category: 'algorithm',
        action: '/gtw/cwai/algorithm/layout/list',
        name: '查询编排算法列表',
        body: { supplier: 'CWAI', algorithmUsage: -1, filePath: '' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'algo-07',
        category: 'algorithm',
        action: '/gtw/cwai/algorithm/layout/detail',
        name: '查询编排算法详情',
        body: { id: 'ALG0000000001', filePath: '/data/algorithm/layout/helmet.json' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },
    {
        id: 'algo-08',
        category: 'algorithm',
        action: '/gtw/cwai/atomic/action/list',
        name: '查询原子动作列表',
        body: { actionUsage: 0, filePath: '' },
        expect: { resCode: 1, timeoutMs: 5000 },
    },

    // ════════════════════════════════════════════════════════════════════
    // 异常场景 (9)
    // ════════════════════════════════════════════════════════════════════
    {
        id: 'err-01',
        category: 'exception',
        action: '/gtw/cwai/Camera/Add',
        mutation: true,
        name: '【异常】必填字段缺失 - 添加通道不带 channelName',
        body: {
            channelType: 0,
            url: 'rtsp://admin:a1234567@192.168.23.113:554/h264/ch1/main/av_stream',
        },
        expect: { resCode: 0, timeoutMs: 5000 },
    },
    {
        id: 'err-02',
        category: 'exception',
        action: '/gtw/cwai/Camera/Page',
        name: '【异常】非法分页参数 - pageNum=0',
        body: { pageNum: 0, pageSize: 10 },
        expect: { resCode: 0, timeoutMs: 5000 },
    },
    {
        id: 'err-03',
        category: 'exception',
        action: '/gtw/cwai/Camera/Delete',
        mutation: true,
        name: '【异常】删除不存在的资源',
        body: { videoChannelId: 'NONEXISTENT_999' },
        expect: { resCode: 0, timeoutMs: 5000 },
    },
    {
        id: 'err-04',
        category: 'exception',
        action: '/gtw/cwai/Camera/Add',
        mutation: true,
        name: '【异常】空 body 对象',
        body: {},
        expect: { resCode: 0, timeoutMs: 5000 },
    },
    {
        id: 'err-05',
        category: 'exception',
        action: '/gtw/cwai/Fake/NonExistent',
        name: '【异常】不存在的 Action',
        body: { dummy: 1 },
        expect: { resCode: 0, timeoutMs: 5000 },
    },
    {
        id: 'err-06',
        category: 'exception',
        action: '/gtw/cwai/Camera/Page',
        name: '【异常】head.msgType 非 request',
        body: { pageNum: 1, pageSize: 10 },
        msgTypeOverride: 'unknown',
        expect: { noReply: true, timeoutMs: 5000 },
    },
    {
        id: 'err-07',
        category: 'exception',
        action: '/gtw/cwai/Camera/Page',
        name: '【异常】deviceSn 不匹配',
        body: { pageNum: 1, pageSize: 10 },
        deviceSnOverride: 'wrong-sn-999',
        expect: { noReply: true, timeoutMs: 5000 },
    },
    {
        id: 'err-08',
        category: 'exception',
        action: '/gtw/cwai/Camera/Page',
        name: '【异常】head 缺 requestId',
        body: { pageNum: 1, pageSize: 10 },
        skipRequestId: true,
        expect: { noReply: true, timeoutMs: 5000 },
    },
    {
        id: 'err-09',
        category: 'exception',
        action: '/gtw/cwai/Camera/Update',
        mutation: true,
        name: '【异常】更新不存在的通道',
        body: {
            videoChannelId: 'NONEXISTENT_999',
            channelType: 0,
            channelName: '不存在的通道',
        },
        expect: { resCode: 0, timeoutMs: 5000 },
    },
];

// ── 辅助方法 ──────────────────────────────────────────────────────────────

/**
 * 获取所有用例总数
 */
function getTotalCount() {
    return testCases.length;
}

/**
 * 按分类获取用例
 * @param {string|string[]} categories - 分类名或分类名数组
 * @returns {Array}
 */
function getByCategory(categories) {
    const cats = Array.isArray(categories) ? categories : [categories];
    return testCases.filter(tc => cats.includes(tc.category));
}

/**
 * 获取所有用例的分类列表
 * @returns {Array<{key: string, label: string, count: number}>}
 */
function getCategories() {
    const catMap = {
        builtin:   { key: 'builtin',   label: '内置协议',   icon: '🔗' },
        'core-task': { key: 'core-task', label: '核心任务',   icon: '📡' },
        system:    { key: 'system',    label: '系统管理',   icon: '⚙️' },
        camera:    { key: 'camera',    label: '摄像机',     icon: '📷' },
        task:      { key: 'task',      label: '任务配置',   icon: '📋' },
        algorithm: { key: 'algorithm', label: '算法管理',   icon: '🧠' },
        exception: { key: 'exception', label: '异常场景',   icon: '⚠️' },
    };
    const counts = {};
    const mutationCounts = {};
    testCases.forEach(tc => {
        counts[tc.category] = (counts[tc.category] || 0) + 1;
        if (tc.mutation) {
            mutationCounts[tc.category] = (mutationCounts[tc.category] || 0) + 1;
        }
    });
    return Object.entries(catMap).map(([key, info]) => ({
        ...info,
        count: counts[key] || 0,
        mutationCount: mutationCounts[key] || 0,
    }));
}

module.exports = {
    TEST_DEVICE_SN,
    testCases,
    getTotalCount,
    getByCategory,
    getCategories,
};
