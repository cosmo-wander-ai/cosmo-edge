#pragma once

#include "Eigen/Dense"

namespace cosmo::nn {

class EigenKalmanFilter {
public:
    EigenKalmanFilter();
    EigenKalmanFilter(const int dynamParams, const int measureParams, const int controlParams = 0);

    ~EigenKalmanFilter();

    void init(const int DP, const int MP, int CP = 0);

    const Eigen::MatrixXf& predict(const Eigen::MatrixXf& control = Eigen::MatrixXf());

    const Eigen::MatrixXf& correct(const Eigen::MatrixXf& measurement);

    Eigen::MatrixXf statePre;             //!< predicted state (x'(k)): x(k)=A*x(k-1)+B*u(k)
    Eigen::MatrixXf statePost;            //!< corrected state (x(k)): x(k)=x'(k)+K(k)*(z(k)-H*x'(k))
    Eigen::MatrixXf transitionMatrix;     //!< state transition matrix (A)
    Eigen::MatrixXf controlMatrix;        //!< control matrix (B) (not used if there is no control)
    Eigen::MatrixXf measurementMatrix;    //!< measurement matrix (H)
    Eigen::MatrixXf processNoiseCov;      //!< process noise covariance matrix (Q)
    Eigen::MatrixXf measurementNoiseCov;  //!< measurement noise covariance matrix (R)
    Eigen::MatrixXf
        errorCovPre;       //!< priori error estimate covariance matrix (P'(k)): P'(k)=A*P(k-1)*At + Q)*/
    Eigen::MatrixXf gain;  //!< Kalman gain matrix (K(k)): K(k)=P'(k)*Ht*inv(H*P'(k)*Ht+R)
    Eigen::MatrixXf
        errorCovPost;  //!< posteriori error estimate covariance matrix (P(k)): P(k)=(I-K(k)*H)*P'(k)

    // temporary matrices
    Eigen::MatrixXf temp1;
    Eigen::MatrixXf temp2;
    Eigen::MatrixXf temp3;
    Eigen::MatrixXf temp4;
    Eigen::MatrixXf temp5;

private:
    int MP;
    int CP;
    int DP;
};

}  // namespace cosmo::nn
