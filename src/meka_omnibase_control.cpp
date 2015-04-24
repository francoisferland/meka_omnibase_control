#include <meka_omnibase_control/meka_omnibase_control.hpp>

using namespace meka_omnibase_control;

bool MekaOmnibaseControl::ReadConfig(const char* filename)
{
    if (!M3Component::ReadConfig(filename)) {
        return false;
    }

    m3joints_name_ = doc["joint_array_component"].as<std::string>();
    m3pwr_name_    = doc["pwr_component"].as<std::string>();

    param_.set_xd_max( doc["param"]["xd_max"].as<double>());
    param_.set_xdd_max(doc["param"]["xdd_max"].as<double>());
    param_.set_td_max( doc["param"]["td_max"].as<double>());
    param_.set_tdd_max(doc["param"]["tdd_max"].as<double>());

    // Make sure there is at least 4 casters defined by looking at the lowest
    // count of all parameters:
    size_t n_casters = doc["param"]["alpha"].size();
    n_casters = std::min(n_casters, doc["param"]["l"].size());
    n_casters = std::min(n_casters, doc["param"]["d"].size());
    n_casters = std::min(n_casters, doc["param"]["r"].size());
    n_casters = std::min(n_casters, doc["param"]["beta_offset"].size());
    if (n_casters != NUM_CASTERS) {
        std::cerr << "MekaOmnibaseControl: Config does not define 4 casters."
                  << std::endl;
        return false;
    }

    using VectorType = omni_kinematics::Robot::VectorType;
    robot_.alpha() = doc["param"]["alpha"].as<VectorType>();
    robot_.l() = doc["param"]["l"].as<VectorType>();
    robot_.d() = doc["param"]["d"].as<VectorType>();
    robot_.r() = doc["param"]["r"].as<VectorType>();

    const std::vector<double>& beta_offset = 
        doc["param"]["beta_offset"].as<VectorType>();
    std::copy(beta_offset.begin(), beta_offset.end(), &beta_offset_[0]);
    const std::vector<double>& beta_ratio = 
        doc["param"]["beta_ratio"].as<VectorType>();
    std::copy(beta_ratio.begin(), beta_ratio.end(), &beta_ratio_[0]);
    const std::vector<double>& phid_ratio = 
        doc["param"]["phid_ratio"].as<VectorType>();
    std::copy(phid_ratio.begin(), phid_ratio.end(), &phid_ratio_[0]);

    robot_.maxBetad() = doc["param"]["betad_max"].as<VectorType>();
    robot_.maxBetadd() = doc["param"]["betadd_max"].as<VectorType>();
    robot_.maxPhid() = doc["param"]["phid_max"].as<VectorType>();
    robot_.maxPhidd() = doc["param"]["phidd_max"].as<VectorType>();
    
    for (int i = 0; i < NUM_CASTERS; ++i) {
        casters_[i].readConfig(doc);
    }

    robot_.calcConstraints();

    return true;
}

bool MekaOmnibaseControl::LinkDependentComponents()
{
    m3joints_ = dynamic_cast<m3::M3JointArray *>(
        factory->GetComponent(m3joints_name_));
    m3pwr_    = (m3::M3Pwr *) factory->GetComponent(m3pwr_name_);

    return (m3joints_ != NULL && m3pwr_ != NULL);
}

void MekaOmnibaseControl::Startup()
{
   for (int i = 0; i < 3; ++i) {
       command_.add_xd_des(0.0);
       status_.add_g_pos(0.0);
       status_.add_l_vel(0.0);
   }
  
   for (int i = 0; i < NUM_CASTERS; ++i) {
       status_.add_beta(0.0);
       status_.add_beta_d(0.0);
       status_.add_phi_d(0.0);

       status_.add_alpha(robot_.alpha()[i]);
       status_.add_l(robot_.l()[i]);

       status_.add_calib(false);
   }

   // NOTE: Cartesian limits currently ignored.
   param_.set_xd_max(0.0);
   param_.set_xdd_max(0.0);
   param_.set_td_max(0.0);
   param_.set_tdd_max(0.0);

   command_.set_ctrl_mode(MEKA_OMNIBASE_CONTROL_OFF);
   last_ctrl_mode_ = MEKA_OMNIBASE_CONTROL_OFF;

   // Reset global odometry.
   robot_.resetPose();
   
}

void MekaOmnibaseControl::Shutdown()
{
}

void MekaOmnibaseControl::StepStatus()
{
    using VectorType = omni_kinematics::Robot::VectorType;
    static VectorType beta(NUM_CASTERS);
    static VectorType betad(NUM_CASTERS);
    static VectorType phi(NUM_CASTERS);
    static VectorType phid(NUM_CASTERS);

    if (IsStateError()) {
        return;
    }

    // Update state in robot model.
    for (int i = 0; i < NUM_CASTERS; ++i) {
        double e[2], ed[2], edd[2];

        m3::M3Joint* joint0 = m3joints_->GetJoint(i*2);
        m3::M3Joint* joint1 = m3joints_->GetJoint(i*2+1);

        // We rely on the first motor encoder for each pair, as a breakbeam
        // sensor is used to 'reliably' locate the zero.
        bool calib = joint0->IsEncoderCalibrated();
        if (!calib) {
            // Encoder not calibrated, turn on the breakbeam sensor.
            joint0->SetLimitSwitchNegZeroEncoder();
        } else {
            // Encoder calibrated, turn off the breakbeam sensor.
            joint0->ClrLimitSwitchNegZeroEncoder();
        }
        status_.set_calib(i, calib);

        e[0]   = joint0->GetThetaRad();
        e[1]   = joint1->GetThetaRad();
        ed[0]  = joint0->GetThetaDotRad();
        ed[1]  = joint1->GetThetaDotRad();
        edd[0] = joint0->GetThetaDotDotRad();
        edd[1] = joint1->GetThetaDotDotRad();

        // Use the caster status update to convert motor velocities to joint
        // velocities (we're interested in what's passed the gearbox).
        casters_[i].stepStatus(e, ed, edd);
        casters_[i].q(beta[i], phi[i]); // phi is unused.
        casters_[i].qd(betad[i], phid[i]);

        // Geometrical transforms to allow reversed directions and sensor
        // offsets:
        beta[i]  = beta_ratio_[i] * omni_kinematics::normalizedAngle(
                                        beta[i] + beta_offset_[i]);
        betad[i] = beta_ratio_[i] * betad[i];

        //std::cerr << "betad: " << betad[i] << " phid:" << phid[i] << std::endl; 

        status_.set_beta(i, beta[i]);
        status_.set_beta_d(i, betad[i]);
        status_.set_phi_d(i, phid[i]);

    }

    robot_.updateState(beta, betad, phid, 1.0 / RT_TASK_FREQUENCY);

    // Update the external status.
    status_.set_l_vel(0, robot_.xd());
    status_.set_l_vel(1, robot_.yd());
    status_.set_l_vel(2, robot_.td());
    status_.set_g_pos(0, robot_.pose().x);
    status_.set_g_pos(1, robot_.pose().y);
    status_.set_g_pos(2, robot_.pose().t);
    
}

void MekaOmnibaseControl::StepCommand()
{
    using VectorType = omni_kinematics::Robot::VectorType;
    using Twist      = omni_kinematics::Twist;

    static Twist twist;
    static VectorType betad(NUM_CASTERS, 0.0);
    static VectorType phid(NUM_CASTERS, 0.0);

    twist.xd = 0.1; //command_.xd_des(0);
    twist.yd = command_.xd_des(1);
    twist.td = command_.xd_des(2);
    ctrl_.saturateTwist(twist, 1.0 / RT_TASK_FREQUENCY, true);

    if (command_.ctrl_mode() == MEKA_OMNIBASE_CONTROL_ON) {
        ctrl_.calcCommand(twist, betad, phid);
/*
        for (int i = 0; i < NUM_CASTERS; ++i) {
            m3joints_->GetJoint(i*2)->SetDesiredControlMode(JOINT_MODE_THETADOT);
            m3joints_->GetJoint(i*2)->SetDesiredThetaDotRad(betad[i]);
            m3joints_->GetJoint(i*2+1)->SetDesiredControlMode(JOINT_MODE_THETADOT);
            m3joints_->GetJoint(i*2+1)->SetDesiredThetaDotRad(phid[i]);
        }
        M3JointArrayCommand* cmd = (M3JointArrayCommand*)m3joints_->GetCommand();
        m3joints_->GetJoint(0)->DisablePwmRamp();
        m3joints_->GetJoint(0)->SetDesiredControlMode(JOINT_MODE_TORQUE);
        m3joints_->GetJoint(0)->SetDesiredTorque(200.0);
        m3joints_->GetJoint(1)->DisablePwmRamp();
        m3joints_->GetJoint(1)->SetDesiredControlMode(JOINT_MODE_TORQUE);
        m3joints_->GetJoint(1)->SetDesiredTorque(00.0);
*/

        std::cerr << "betas: " << robot_.beta()[0] << ", "
                               << robot_.beta()[1] << ", "
                               << robot_.beta()[2] << ", "
                               << robot_.beta()[3] << ", "
                  << std::endl; 



        M3JointArrayCommand* cmd = (M3JointArrayCommand*)m3joints_->GetCommand();
        for (int i = 0; i < NUM_CASTERS; ++i) {

            if (last_ctrl_mode_ != MEKA_OMNIBASE_CONTROL_ON) {
                casters_[i].reset();
            }

            double tq0, tq1;

            casters_[i].stepCommand(betad[i], phid[i]);
            casters_[i].tq(tq0, tq1);

            //std::cerr << "betad: " << robot_.betad()[i] 
            //          << " phid:"  << robot_.phid()[i] 
            //          << std::endl; 
            //std::cerr << "tqs: " << tq0 << " " << tq1 << std::endl; 
            // TEMP:
            //tq0 = 0.0;

            /*
            if (i != 3) {
                tq0 = tq1 = 0.0;
            } else {
                std::cerr << "betad des: " << betad[i] 
                          << " phid des: " << phid[i]
                          << std::endl;
            }
            */
            m3joints_->GetJoint(i*2  )->DisablePwmRamp();   // Make sure this is necessary
            m3joints_->GetJoint(i*2+1)->DisablePwmRamp();
            cmd->set_ctrl_mode(i*2,    JOINT_ARRAY_MODE_TORQUE);
            cmd->set_ctrl_mode(i*2+1,  JOINT_ARRAY_MODE_TORQUE);
            cmd->set_tq_desired(i*2,   -tq0);
            cmd->set_tq_desired(i*2+1, -tq1);
        }
        

    } else {
        for (int i = 0; i < NUM_CASTERS*2; ++i) {
            m3joints_->GetJoint(i)->SetDesiredControlMode(JOINT_MODE_OFF);
        }
    }

    last_ctrl_mode_ = command_.ctrl_mode();
}

