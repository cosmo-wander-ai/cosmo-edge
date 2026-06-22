#include "nn/utils/tracker/eigen_kalman_filter.h"

namespace cosmo::nn {

EigenKalmanFilter::EigenKalmanFilter() {}

EigenKalmanFilter::~EigenKalmanFilter() {}

EigenKalmanFilter::EigenKalmanFilter(const int dynamParams, const int measureParams,
                                     const int controlParams) {
    init(dynamParams, measureParams, controlParams);
    DP = dynamParams;
    MP = measureParams;
}

void EigenKalmanFilter::init(const int DP, const int MP, int CP) {
    CP = std::max(CP, 0);

    statePre         = Eigen::MatrixXf::Zero(DP, 1);
    statePost        = Eigen::MatrixXf::Zero(DP, 1);
    transitionMatrix = Eigen::MatrixXf().setIdentity(DP, DP);

    processNoiseCov     = Eigen::MatrixXf().setIdentity(DP, DP) * 0.01;
    measurementMatrix   = Eigen::MatrixXf::Zero(MP, DP);
    measurementNoiseCov = Eigen::MatrixXf().setIdentity(MP, MP) * 0.01;

    errorCovPre  = Eigen::MatrixXf::Zero(DP, DP);
    errorCovPost = Eigen::MatrixXf::Zero(DP, DP);
    gain         = Eigen::MatrixXf::Zero(DP, MP);

    if (CP > 0)
        controlMatrix = Eigen::MatrixXf::Zero(DP, CP);

    temp1 = Eigen::MatrixXf::Zero(DP, DP);
    temp2 = Eigen::MatrixXf::Zero(MP, DP);
    temp3 = Eigen::MatrixXf::Zero(MP, MP);
    temp4 = Eigen::MatrixXf::Zero(MP, DP);
    temp5 = Eigen::MatrixXf::Zero(MP, 1);
}

const Eigen::MatrixXf& EigenKalmanFilter::predict(const Eigen::MatrixXf& control) {
    // update the state: x'(k) = A*x(k)
    statePre = transitionMatrix * statePost;

    if (control.size() > 0)
        // x'(k) = x'(k) + B*u(k)
        statePre += controlMatrix * control;
    // update error covariance matrices: temp1 = A*P(k)
    temp1 = transitionMatrix * errorCovPost;

    // P'(k) = temp1*At + Q
    errorCovPre = temp1 * transitionMatrix.transpose() + processNoiseCov;

    // handle the case when there will be measurement before the next predict.
    statePost    = statePre;
    errorCovPost = errorCovPre;
    return statePre;
}

const Eigen::MatrixXf& EigenKalmanFilter::correct(const Eigen::MatrixXf& measurement) {
    temp2 = measurementMatrix * errorCovPre;
    // temp3 = temp2*Ht + R
    Eigen::MatrixXf temp3 = temp2 * measurementMatrix.transpose() + measurementNoiseCov;
    // temp3 = temp3_.block(0, 0, MP, MP);

    // temp4 = inv(temp3)*temp2 = Kt(k)
    temp4 = temp3.inverse() * temp2;

    // K(k)
    gain = temp4.transpose();

    // temp5 = z(k) - H*x'(k)
    temp5 = measurement - measurementMatrix * statePre;

    // x(k) = x'(k) + K(k)*temp5
    statePost = statePre + gain * temp5;

    // P(k) = P'(k) - K(k)*temp2
    errorCovPost = errorCovPre - gain * temp2;
    return statePost;
}

}  // namespace cosmo::nn