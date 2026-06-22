// ErrorCode — Segmented error codes. E.g. generic error codes are 0x0-0x10000. Other types ...

#include "util/ErrorCode.h"

namespace cosmo::util {

template <typename T = void>
std::string Translate(const std::string& str) {
    return str;
}

template <>
inline std::string Translate<void>(const std::string& str) {
    return str;
}

struct ErrorCategory : public std::error_category {
    const char* name() const noexcept override;
    std::string message(int) const override;
};

namespace {
    const ErrorCategory& GetErrorCategory() {
        static ErrorCategory cate{};
        return cate;
    }
}  // namespace

std::error_condition make_error_condition(cosmo::util::ErrorEnum code) {
    return {static_cast<int>(code), GetErrorCategory()};
}

const char* ErrorCategory::name() const noexcept {
    return "CWAI Error";
}

#define CaseStr(X, Y)                                                                                        \
    case cosmo::util::ErrorEnum::X:                                                                          \
        return Translate<cosmo::util::ErrorEnum>(Y)

#define Case(X) CaseStr(X, #X)

std::string ErrorCategory::message(int code) const {
    switch (static_cast<cosmo::util::ErrorEnum>(code)) {
        CaseStr(Success, "成功");
        CaseStr(NotImplement, "接口未实现");
        CaseStr(Created, "目标已经被创建");
        CaseStr(NotCreated, "目标未创建");
        CaseStr(NotInit, "目标未初始化");
        CaseStr(Failed, "失败");
        CaseStr(TimeSyncFailed, "时间同步失败");
        CaseStr(TaskCreateFailed, "任务创建失败");
        CaseStr(ResourceLimit, "资源不足");
        CaseStr(CreateToManyTimes, "任务重建失败");
        CaseStr(SysErr, "系统错误");
        CaseStr(NoMem, "没有足够的内存");
        CaseStr(InValidMem, "无效的内存");
        CaseStr(InvalidParam, "无效的参数");
        CaseStr(MandatoryParamMiss, "必要参数缺失");
        CaseStr(FileOpenFailed, "文件打开失败");
        CaseStr(FileNotOpened, "文件未打开就读取");
        CaseStr(FileNotExist, "文件不存在");
        CaseStr(FileNotSupport, "文件不支持");
        CaseStr(FileSizeSmall, "文件尺寸太小");
        CaseStr(FileSizeBig, "文件尺寸太大");
        CaseStr(FileDecryptFailed, "文件解密失败");
        CaseStr(FileAnalysisFailed, "文件解析失败");
        CaseStr(LicenseDeviceNotMatch, "授权文件不匹配");
        CaseStr(ParameterException, "参数异常");
        CaseStr(ParameterLenError, "参数长度不合法");
        CaseStr(AlgProcessException, "流程不支持");
        CaseStr(InvalidImage, "输入图像不合法或者空");
        CaseStr(FlowDataNull, "动作流数据为空");
        CaseStr(FlowDataInvalid, "动作流数据异常");
        CaseStr(FrameDataInvalid, "帧数据异常");
        CaseStr(FlowDataInputInvalid, "动作流输入数据异常");
        CaseStr(ServiceNotInit, "服务未启动");
        CaseStr(DependServiceNotInit, "依赖的服务未启动");
        CaseStr(DependLibEmpty, "依赖的库为空");
        CaseStr(PersonLibNotEmpty, "工服库删除失败，工服库绑定了工服照");
        CaseStr(NotSupport, "系统支持问题");
        CaseStr(FileMoveFailed, "文件移动失败");
        CaseStr(UpgradeFileVerifyFailed, "安装包校验错误");
        CaseStr(UpgradeFileChipType, "安装包芯片类型错误");
        CaseStr(UpgradeFileNotMatch, "安装包信息不匹配");
        CaseStr(UpgradeFileVersion, "安装包版本不匹配");
        CaseStr(UpgradeHaveTasks, "有升级任务");
        CaseStr(UpLoadDataEmpty, "上传的数据为空");
        CaseStr(FileTypeParamEmpty, "上传的文件类型为空");
        CaseStr(IpProbeFailed, "IP 探测错误");
        CaseStr(MaxCountLimit, "最大数量限制");
        CaseStr(NoSuchId, "未找到ID");
        CaseStr(ExistedId, "ID重复");
        CaseStr(ExistedName, "名称重复");
        CaseStr(FaceLibNotEmpty, "脸库不为空，不能删除");
        CaseStr(FaceLibCountOverFlow, "脸库数量达到上限");
        CaseStr(DebugQuit, "测试退出");
        CaseStr(InterfaceNotSupport, "接口不支持");
        CaseStr(PersonLibOverflow, "工服库单库工服照达到上限");
        CaseStr(ArticlesReidLibNotEmpty, "物品库删除失败，物品库绑定了物品照");
        CaseStr(PersonInAllLibOverflow, "各工服库工服照总数达到上限");
        CaseStr(PersonPicTargetOverflow, "单张照片工服数超过1");
        CaseStr(PersonNotDetected, "照片未检测到人体工服");
        CaseStr(PersonPicSizeIllegal, "工服照片像素不合格");
        CaseStr(PersonPicQualityIllegal, "工服照片质量不合格");
        CaseStr(DecodeFailed, "图像解码失败");
        CaseStr(EncodeFailed, "编码路数超过上线");
        CaseStr(UnknownOperation, "操作类型错误");

        CaseStr(AuthFailed, "鉴权失败");
        CaseStr(LoginFailed, "登陆失败");
        CaseStr(NotLogin, "未登录");
        CaseStr(OldPasswdWrong, "旧密码错误");
        CaseStr(LoginFrequence, "登陆太频繁");
        CaseStr(UnZipFileFailed, "解压文件失败");
        CaseStr(ModelFileName, "模型文件名不对");
        CaseStr(ModelFileLack, "模型文件缺失");
        CaseStr(ModelFileAnalysis, "模型文件解析出错");
        CaseStr(ModelFileNotMatch, "模型文件不匹配");
        CaseStr(ModelFilePlatform, "模型文件不适配该平台");
        CaseStr(IDNotExist, "配置不存在");
        CaseStr(IDExist, "配置已存在，不允许重复添加");
        CaseStr(TimeTemplateNotExist, "时间模板不存在，不允许设置");
        CaseStr(TimeTemplateInUse, "时间模板使用中，不允许删除");
        CaseStr(TimeTemplateCountLimit, "时间模板数量超过上限");
        CaseStr(DefaultCantBeDelete, "默认不可删除");
        CaseStr(DefaultCantBeUpdate, "默认不可修改");
        CaseStr(CameraNotExist, "相机不存在");
        CaseStr(CameraNotOnline, "相机不在线");
        CaseStr(CameraCountLimit, "相机数量达到上限");
        CaseStr(TaskNotExist, "任务不存在");
        CaseStr(AlgorithmNotExist, "算法不存在");
        CaseStr(AlgorithmInUse, "算法正在被使用，删除失败，请先在任务中删除该算法");
        CaseStr(TaskTooMuch, "通道下的任务数太多");
        CaseStr(TooMuchAreas, "区域太多");
        CaseStr(LackArea, "缺少区域");
        CaseStr(DatabaseFailed, "数据库操作失败");
        CaseStr(MvDebugModel, "调试模式.......无法响应正常消息");

        CaseStr(ActionReady, "动作已初始化未启动");
        CaseStr(ActionStart, "动作已启动");
        CaseStr(ActionStop, "动作已停止");

        CaseStr(VideoResolutionNotSupport, "视频分辨率不支持");
        CaseStr(VideoFormatNotSupport, "视频格式不支持");

        CaseStr(DemuxOpenInvalidUrl, "流地址无效");
        CaseStr(DemuxOpenStreamFail, "取流打开流失败");
        CaseStr(DemuxOpenStreamUnauthorized, "取流校验失败");
        CaseStr(DemuxFindStreamFail, "取流查找流失败");
        CaseStr(DemuxFindVideoStreamFail, "取流查找流失败");
        CaseStr(DemuxInitBsfcFail, "取流查找流失败");
        CaseStr(DemuxGetStreamFail, "取流查找流失败");
        CaseStr(DemuxStreamStart, "开始取流");
        CaseStr(DemuxStreamClosed, "取流结束");
        CaseStr(DemuxReadStreamFail, "读流失败");
        CaseStr(DemuxNoData, "取流无数据");
        CaseStr(DecoderColorConvertFailed, "图像色彩转换失败");
        CaseStr(DecoderResizePaddingFailed, "图像Padding失败");
        CaseStr(DecoderFrameFailed, "解码帧数据失败");

        CaseStr(ImageDecodeFailed, "图像解码失败");
        CaseStr(ImageDownloadFailed, "图像下载失败");
        CaseStr(ImageResolutionInvalid, "图像分辨率不合法");
        CaseStr(ImageContentSizeInvalid, "图像内容大小不合法");
        CaseStr(ImageContentDecryptionFailed, "图像内容解密失败");
        CaseStr(ImageChannelCountInvalid, "图像通道数量不合法");
        CaseStr(ImageReadFailed, "照片读取失败");
        CaseStr(ImageCatchFailed, "通道照片抓取失败");
        CaseStr(ImageEncodeFailed, "图片编码失败");

        CaseStr(FaceIsNotInTheMiddle, "人脸需要在照片的中间");
        CaseStr(FaceIsTooSmall, "人脸像素小于120*120，请重新上传");
        CaseStr(FaceIsIncomplete, "人脸位置越过图像边界");
        CaseStr(QualityNotEnough, "质量不足80分，请重新上传");
        CaseStr(GetFeatureFailed, "特征值提取失败");
        CaseStr(InternalError, "内部错误，请重试");
        CaseStr(NoFaceDetected, "未检测到人脸，请重新上传");

        CaseStr(ArticlesReidLibCountOverFlow, "物品库数量达到上限");
        CaseStr(ArticlesReidPicSizeIllegal, "物品照片像素不合格");
        CaseStr(ArticlesReidLibOverflow, "物品库单库物品照达到上限");
        CaseStr(ArticlesReidInAllLibOverflow, "各物品库物品照总数达到上限");
        CaseStr(ArticlesReidElementNotEmpty, "物品元素不能为空");

        CaseStr(StrategyNotExist, "外设编排策略不存在");

        CaseStr(NoAuthService, "服务未授权");
        CaseStr(AuthExpiredService, "服务授权过期");
        CaseStr(AuthServiceCountLimit, "授权数量不够");
        CaseStr(AuthServiceFileAnalysisFailed, "授权文件解析失败");

        CaseStr(AI_FAILED, "AI错误");
        CaseStr(AI_INST_CREATED, "AI实例已创建");
        CaseStr(AI_INST_CREATEFAILED, "AI实例创建失败");
        CaseStr(AI_INST_NOTCREATED, "AI实例未创建");
        CaseStr(AI_INIT_PARAMERR, "AI初始化参数错误");
        CaseStr(AI_DETECT_FAILED, "AI检测错误");
        CaseStr(AI_CLASSIFY_FAILED, "AI分类错误");
        CaseStr(AI_LANDMARK_FAILED, "AI关键点错误");
        CaseStr(AI_FEATURE_FAILED, "AI特征提取错误");
        CaseStr(AI_TRACK_FAILED, "AI追踪错误");
        CaseStr(AI_TRACK_PARAM_INVALID, "AI追踪错误");
        CaseStr(AI_FORWARD_FAILED, "AI推理错误");
        CaseStr(AI_PARSE_OUTPUT_FAILED, "AI结果解析错误");

        CaseStr(OperationNotSupport, "操作不支持");
        CaseStr(ExistedPersonSerialNumber, "人员编号重复");
        CaseStr(PolygonIntersection, "多边形相交");
        CaseStr(VcpCheckFailed, "云升级版本检查失败");
        CaseStr(AuthApiCallFailed, "授权平台接口调用失败");
        CaseStr(AuthSignVerifyFailed, "签名验证失败");
        CaseStr(AuthQueryFailed, "算法授权失败");
        CaseStr(AuthServiceFailed, "服务授权失败");
        CaseStr(AuthPlatFailed, "授权失败（授权码错误或余额不足等）");
        CaseStr(AuthEmpty, "授权服务为空");

        CaseStr(ActionAlgNotExist, "编排算法不存在");
        CaseStr(ActionAlgDownLoadFailed, "编排算法下载失败");
        CaseStr(ActionAlgGetAtomicCodeList, "编排算法获取原子算法列表失败");
        CaseStr(ActionAlgCreateFailed, "编排算法创建失败");
        CaseStr(ActionAlgLoadFailed, "编排算法加载失败");
        CaseStr(ActionFailed, "算法编排失败");
        CaseStr(ActionAlgArrangeConfigFail, "算法配置读取失败");
        default:
            break;
    }
    return {};
}
}  // namespace cosmo::util
