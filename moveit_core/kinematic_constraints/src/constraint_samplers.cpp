/*********************************************************************
* Software License Agreement (BSD License)
* 
*  Copyright (c) 2011, Willow Garage, Inc.
*  All rights reserved.
* 
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
* 
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
* 
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Author: Ioan Sucan */

#include "kinematic_constraints/constraint_samplers.h"
#include <boost/math/constants/constants.hpp>
#include <ros/console.h>
#include <algorithm>
#include <set>

kinematic_constraints::ConstraintSampler::ConstraintSampler(const planning_models::KinematicModel::JointModelGroup *jmg) : jmg_(jmg)
{
    if (!jmg_)
        ROS_FATAL("NULL group specified for constraint sampler");
}

kinematic_constraints::ConstraintSampler::~ConstraintSampler(void)
{
}

kinematic_constraints::JointConstraintSampler::JointConstraintSampler(const planning_models::KinematicModel::JointModelGroup *jmg,
                                                                      const std::vector<JointConstraint> &jc) : ConstraintSampler(jmg)
{
    // find and keep the constraints that operate on the group we sample
    // also keep bounds for joints as convenient
    const std::map<std::string, unsigned int> &vim = jmg_->getJointVariablesIndexMap();
    std::set<const planning_models::KinematicModel::JointModel*> bounded;
    for (std::size_t i = 0 ; i < jc.size() ; ++i)
    {
        if (!jc[i].enabled())
            continue;
        const planning_models::KinematicModel::JointModel *jm = jc[i].getJointModel();
        if (!jmg_->hasJointModel(jm->getName()))
            continue;
        std::pair<double, double> bounds;
        jm->getVariableBounds(jm->getName(), bounds);
        bounds.first = std::max(bounds.first, jc[i].getDesiredJointPosition() - jc[i].getJointToleranceBelow());
        bounds.second = std::min(bounds.second, jc[i].getDesiredJointPosition() + jc[i].getJointToleranceAbove());
        if (bounds.first > bounds.second)
            ROS_WARN_STREAM("The constraints for joint '" << jm->getName() << "' are such that there are no possible values for this joint. Ignoring constraint.");
        else
        {
            bounded.insert(jm);        
            bounds_.push_back(bounds);
            index_.push_back(vim.find(jm->getName())->second);
        }
    }
    
    // get a separate list of joints that are not bounded; we will sample these randomly
    const std::vector<const planning_models::KinematicModel::JointModel*> &joints = jmg_->getJointModels();
    for (std::size_t i = 0 ; i < joints.size() ; ++i)
        if (bounded.find(joints[i]) == bounded.end())
        {
            unbounded_.push_back(joints[i]);
            uindex_.push_back(vim.find(joints[i]->getName())->second);
        }
}

bool kinematic_constraints::JointConstraintSampler::sample(std::vector<double> &values, const planning_models::KinematicState * /* ks */,
							   unsigned int /* max_attempts */)
{
    values.resize(jmg_->getVariableCount());    
    // enforce the constraints for the constrained components (could be all of them)
    for (std::size_t i = 0 ; i < bounds_.size() ; ++i)
        values[index_[i]] = rng_.uniformReal(bounds_[i].first, bounds_[i].second);
    
    // sample the rest of the components
    for (std::size_t i = 0 ; i < unbounded_.size() ; ++i)
    {
        std::vector<double> v;
        unbounded_[i]->getRandomValues(rng_, v);
        for (std::size_t j = 0 ; j < v.size() ; ++j)
            values[uindex_[i] + j] = v[j];
    }
    
    // we are always successful
    return true;
}

kinematic_constraints::IKSamplingPose::IKSamplingPose()
{
}

kinematic_constraints::IKSamplingPose::IKSamplingPose(const PositionConstraint &pc) : pc_(new PositionConstraint(pc))
{
}

kinematic_constraints::IKSamplingPose::IKSamplingPose(const OrientationConstraint &oc) : oc_(new OrientationConstraint(oc))
{
}

kinematic_constraints::IKSamplingPose::IKSamplingPose(const PositionConstraint &pc, const OrientationConstraint &oc) : 
    pc_(new PositionConstraint(pc)), oc_(new OrientationConstraint(oc))
{
}

kinematic_constraints::IKSamplingPose::IKSamplingPose(const boost::shared_ptr<PositionConstraint> &pc) : pc_(pc)
{
}

kinematic_constraints::IKSamplingPose::IKSamplingPose(const boost::shared_ptr<OrientationConstraint> &oc) : oc_(oc)
{
}

kinematic_constraints::IKSamplingPose::IKSamplingPose(const boost::shared_ptr<PositionConstraint> &pc, const boost::shared_ptr<OrientationConstraint> &oc) : pc_(pc), oc_(oc)
{
}

kinematic_constraints::IKConstraintSampler::IKConstraintSampler(const planning_models::KinematicModel::JointModelGroup *jmg, 
								const IKAllocator &ik_alloc, const IKSamplingPose &sp) :
    ConstraintSampler(jmg), ik_alloc_(ik_alloc), sp_(sp), ik_timeout_(0.5)
{
    if (sp_.pc_ && sp_.oc_)
	if (sp_.pc_->getLinkModel()->getName() != sp_.oc_->getLinkModel()->getName())
	    ROS_FATAL("Position and orientation constraints need to be specified for the same link in order to use IK-based sampling");
}

bool kinematic_constraints::IKConstraintSampler::initialize(void)
{
    if (kb_)
	return true;
    return loadIKSolver();
}

double kinematic_constraints::IKConstraintSampler::getSamplingVolume(void) const
{
    double v = 1.0;
    if (sp_.pc_)
	if (sp_.pc_->getConstraintRegion())
	    v *= sp_.pc_->getConstraintRegion()->computeVolume();
    if (sp_.oc_)
	v *= sp_.oc_->getRollTolerance() * sp_.oc_->getPitchTolerance() * sp_.oc_->getYawTolerance();
    return v;
}

const std::string& kinematic_constraints::IKConstraintSampler::getLinkName(void) const
{
    if (sp_.oc_)
	return sp_.oc_->getLinkModel()->getName();
    return sp_.pc_->getLinkModel()->getName();    
}

bool kinematic_constraints::IKConstraintSampler::loadIKSolver(void)
{
    if (!ik_alloc_)
    {
        ROS_ERROR("No IK allocator specified");
        return false;
    }
    
    // allocate the solver
    kb_ = ik_alloc_(jmg_);
    if (!kb_)
    {
        ROS_ERROR("Failed to allocate IK solver");
        return false;
    }
    else
        ROS_DEBUG("IKConstraintSampler successfully loaded IK solver");
    
    // the ik solver must cover the same joints as the group
    const std::vector<std::string> &ik_jnames = kb_->getJointNames();
    const std::map<std::string, unsigned int> &g_map = jmg_->getJointVariablesIndexMap();
    
    if (ik_jnames.size() != g_map.size())
    {
        ROS_ERROR_STREAM("Group '" << jmg_->getName() << "' does not have the same set of joints as the employed IK solver");
        return false;
    }
    
    // compute a mapping between the group state and the IK solution
    ik_joint_bijection_.clear();    
    for (std::size_t i = 0 ; i < ik_jnames.size() ; ++i)
    {
        std::map<std::string, unsigned int>::const_iterator it = g_map.find(ik_jnames[i]);
        if (it == g_map.end())
        {
            ROS_ERROR_STREAM("IK solver computes joint values for joint '" << ik_jnames[i] << "' but group '" << jmg_->getName() << "' does not contain such a joint.");
            return false;
        }
        const planning_models::KinematicModel::JointModel *jm = jmg_->getJointModel(ik_jnames[i]);
        for (unsigned int k = 0 ; k < jm->getVariableCount() ; ++k)
            ik_joint_bijection_.push_back(it->second + k);
    }

    // check if we need to transform the request into the coordinate frame expected by IK
    ik_frame_ = kb_->getBaseFrame();
    transform_ik_ = ik_frame_ != jmg_->getParentModel()->getModelFrame();
    if (transform_ik_)
        if (!jmg_->getParentModel()->hasLinkModel(ik_frame_))
        {
            ROS_ERROR_STREAM("The IK solver expects requests in frame '" << ik_frame_ << "' but this frame is not known to the sampler. Ignoring transformation (IK may fail)");
            transform_ik_ = false;
        }

    // check if IK is performed for the desired link
    bool wrong_link = false;
    if (sp_.pc_)
    {
        if (kb_->getTipFrame() != sp_.pc_->getLinkModel()->getName())
            wrong_link = true;
    }
    else
        if (kb_->getTipFrame() != sp_.oc_->getLinkModel()->getName())
            wrong_link = true;
    if (wrong_link)
    {
        ROS_ERROR("IK cannot be performed for link '%s'. The solver can report IK solutions for link '%s'.", 
                  sp_.pc_ ? sp_.pc_->getLinkModel()->getName().c_str() : sp_.oc_->getLinkModel()->getName().c_str(), kb_->getTipFrame().c_str());
        return false;
    }
    
    return true;
}

bool kinematic_constraints::IKConstraintSampler::sample(std::vector<double> &values, const planning_models::KinematicState *ks,
							unsigned int max_attempts)
{
    // make sure we at least have a chance of sampling using IK; we need at least some kind of constraint
    if (!sp_.pc_ && !sp_.oc_)
        return false;

    // load an IK solver if we need to 
    if (!kb_)
        if (!loadIKSolver())
        {
            kb_.reset();
            return false;
        }    
    
    for (unsigned int a = 0 ; a < max_attempts ; ++a)
    {
        // sample a point in the constraint region
        btVector3 point;
        if (sp_.pc_)
        {
            if (!sp_.pc_->getConstraintRegion()->samplePointInside(rng_, max_attempts, point))
                return false;
        }
        else
        {
            // do FK for rand state
            if (!ks)
            {
                ROS_WARN("An initial kinematic state is needed to sample possible link positions");
                return false;
            }
            planning_models::KinematicState tempState(*ks);
            planning_models::KinematicState::JointStateGroup *tmp = tempState.getJointStateGroup(jmg_->getName());
            if (tmp)
            {
                tmp->setRandomValues();
                point = tempState.getLinkState(sp_.oc_->getLinkModel()->getName())->getGlobalLinkTransform().getOrigin();
            }
            else
            {
                ROS_ERROR("Passed a JointStateGroup for a mismatching JointModelGroup");
                return false;
            }
        }
        
        btQuaternion quat;
        if (sp_.oc_)
        {
            // sample a rotation matrix within the allowed bounds
            double rpy[3];
            rng_.eulerRPY(rpy);
            btMatrix3x3 diff;
            diff.setEulerYPR(rpy[2] * sp_.oc_->getYawTolerance() / boost::math::constants::pi<double>(),
                             rpy[1] * sp_.oc_->getPitchTolerance() / boost::math::constants::pi<double>(),
                             rpy[0] * sp_.oc_->getRollTolerance() / boost::math::constants::pi<double>());
            (sp_.oc_->getDesiredRotationMatrix() * diff).getRotation(quat);
        }
        else
        {
            // sample a random orientation
            double q[4];
            rng_.quaternion(q);
            quat = btQuaternion(q[0], q[1], q[2], q[3]);
        }
        
        // if there is an offset, we need to undo the induced rotation in the sampled transform origin (point)
        if (sp_.pc_ && sp_.pc_->hasLinkOffset())
            // the rotation matrix that corresponds to the desired orientation
            point = point - btMatrix3x3(quat) * sp_.pc_->getLinkOffset();

        // we now have the transform we wish to perform IK for, in the planning frame
        
        if (transform_ik_ && ks)
        {
            // we need to convert this transform to the frame expected by the IK solver
            // both the planning frame and the frame for the IK are assumed to be robot links
            btTransform ikq(quat, point);
            
            const planning_models::KinematicState::LinkState *ls = ks->getLinkState(ik_frame_);
            ikq = ls->getGlobalLinkTransform().inverse() * ikq;
            
            point = ikq.getOrigin();
            quat = ikq.getRotation();
        }

        geometry_msgs::Pose ik_query;
        ik_query.position.x = point.x();
        ik_query.position.y = point.y();
        ik_query.position.z = point.z();
        ik_query.orientation.x = quat.x();
        ik_query.orientation.y = quat.y();
        ik_query.orientation.z = quat.z();
        ik_query.orientation.w = quat.w();        
        
        if (callIK(ik_query, ik_timeout_, values))
            return true;
    }
    return false;
}

bool kinematic_constraints::IKConstraintSampler::callIK(const geometry_msgs::Pose &ik_query, double timeout, std::vector<double> &solution)
{
    // sample a seed value
    std::vector<double> vals;
    jmg_->getRandomValues(rng_, vals);
    ROS_ASSERT(vals.size() == ik_joint_bijection_.size());
    std::vector<double> seed(ik_joint_bijection_.size(), 0.0);
    for (std::size_t i = 0 ; i < ik_joint_bijection_.size() ; ++i)
        seed[ik_joint_bijection_[i]] = vals[i];
    
    std::vector<double> ik_sol;
    moveit_msgs::MoveItErrorCodes error;
    
    if (kb_->searchPositionIK(ik_query, seed, timeout, ik_sol, error))
    {
        ROS_ASSERT(ik_sol.size() == ik_joint_bijection_.size());
        solution.resize(ik_joint_bijection_.size());
        for (std::size_t i = 0 ; i < ik_joint_bijection_.size() ; ++i)
            solution[i] = ik_sol[ik_joint_bijection_[i]];
        return true;
    }
    else
        ROS_DEBUG("IK solver failed with error %d", error.val);
    return false;
}

kinematic_constraints::UnionConstraintSampler::UnionConstraintSampler(const planning_models::KinematicModel::JointModelGroup *jmg, std::vector<ConstraintSamplerPtr> &samplers) :
    ConstraintSampler(jmg), samplers_(samplers)
{
    const std::map<std::string, unsigned int> &gi = jmg->getJointVariablesIndexMap();
    for (std::size_t i = 0 ; i < samplers.size() ; ++i)
    {
	bijection_[i].resize(gi.size(), -1);
	const std::map<std::string, unsigned int> &sgi = samplers[i]->getJointModelGroup()->getJointVariablesIndexMap();
	for (std::map<std::string, unsigned int>::const_iterator it = sgi.begin() ; it != sgi.end() ; ++it)
	{
	    std::map<std::string, unsigned int>::const_iterator jt = gi.find(it->first);
	    if (jt == gi.end())
		ROS_FATAL("Subgroups do not match group");
	    bijection_[i][it->second] = jt->second;
	}
    }    
}

bool kinematic_constraints::UnionConstraintSampler::sample(std::vector<double> &values, const planning_models::KinematicState *ks, unsigned int max_attempts)
{
    for (std::size_t i = 0 ; i < samplers_.size() ; ++i)
    {
	std::vector<double> v;
	if (!samplers_[i]->sample(v, ks, max_attempts))
	    return false;
	for (std::size_t j = 0 ; j < v.size() ; ++j)
	    values[bijection_[i][j]] = v[j];
    }
    return true;
}

kinematic_constraints::ConstraintSamplerPtr kinematic_constraints::constructConstraintsSampler(const planning_models::KinematicModel::JointModelGroup *jmg,
											       const moveit_msgs::Constraints &constr,
											       const planning_models::KinematicModelConstPtr &kmodel,
											       const planning_models::TransformsConstPtr &ftf,
											       const IKAllocator &ik_alloc,
											       const IKSubgroupAllocator &ik_subgroup_alloc)
{
    ConstraintSamplerPtr joint_sampler;
    // if there are joint constraints, we could possibly get a sampler from those
    if (!constr.joint_constraints.empty())
    {
	// construct the constraints
        std::vector<JointConstraint> jc;
        for (std::size_t i = 0 ; i < constr.joint_constraints.size() ; ++i)
        {
            JointConstraint j(kmodel, ftf);
            if (j.use(constr.joint_constraints[i]))
                jc.push_back(j);
        }

	// if we have constrained every joint, then we just use a sampler using these constraints
	if (jc.size() == jmg->getJointModels().size())
	{
	    ROS_DEBUG("Allocated a sampler satisfying joint constraints for group '%s'", jmg->getName().c_str());
	    return ConstraintSamplerPtr(new JointConstraintSampler(jmg, jc));
	}
	// if a smaller set of joints has been specified, keep the constraint sampler around, but use it only if no IK sampler has been specified.
	if (!jc.empty())
	    joint_sampler.reset(new JointConstraintSampler(jmg, jc));
    }
    
    // if we have a means of computing complete states for the group using IK, then we try to see if any IK constraints should be used
    if (ik_alloc)
    {	
	// keep track of which links we constrained
	std::map<std::string, IKConstraintSampler*> usedL;
	
	// if we have position and/or orientation constraints on links that we can perform IK for,
	// we will use a sampleable goal region that employs IK to sample goals;
	// if there are multiple constraints for the same link, we keep the one with the smallest 
	// volume for sampling
	for (std::size_t p = 0 ; p < constr.position_constraints.size() ; ++p)
	    for (std::size_t o = 0 ; o < constr.orientation_constraints.size() ; ++o)
		if (constr.position_constraints[p].link_name == constr.orientation_constraints[o].link_name)
		{
		    boost::shared_ptr<PositionConstraint> pc(new PositionConstraint(kmodel, ftf));
		    boost::shared_ptr<OrientationConstraint> oc(new OrientationConstraint(kmodel, ftf));
		    if (pc->use(constr.position_constraints[p]) && oc->use(constr.orientation_constraints[o]))
		    {
			IKConstraintSampler *iks = new IKConstraintSampler(jmg, ik_alloc, IKSamplingPose(pc, oc));
			if (iks->initialize())
			{
			    bool use = true;
			    if (usedL.find(constr.position_constraints[p].link_name) != usedL.end())
				if (usedL[constr.position_constraints[p].link_name]->getSamplingVolume() < iks->getSamplingVolume())
				    use = false;
			    if (use)
			    {
				usedL[constr.position_constraints[p].link_name] = iks;
				ROS_DEBUG("Allocated an IK-based sampler for group '%s' satisfying position and orientation constraints on link '%s'",
					  jmg->getName().c_str(), constr.position_constraints[p].link_name.c_str());
			    }
			    else
				delete iks;
			}
			else
			    delete iks;
		    }
		}
	
	for (std::size_t p = 0 ; p < constr.position_constraints.size() ; ++p)
	{
	    boost::shared_ptr<PositionConstraint> pc(new PositionConstraint(kmodel, ftf));
	    if (pc->use(constr.position_constraints[p]))
	    {
		IKConstraintSampler *iks = new IKConstraintSampler(jmg, ik_alloc, IKSamplingPose(pc));
		if (iks->initialize())
		{
		    bool use = true;
		    if (usedL.find(constr.position_constraints[p].link_name) != usedL.end())
			if (usedL[constr.position_constraints[p].link_name]->getSamplingVolume() < iks->getSamplingVolume())
			    use = false;
		    if (use)
		    {
			usedL[constr.position_constraints[p].link_name] = iks;
			ROS_DEBUG("Allocated an IK-based sampler for group '%s' satisfying position constraints on link '%s'", 
				  jmg->getName().c_str(), constr.position_constraints[p].link_name.c_str());
		    }
		    else
			delete iks;
		}
		else
		    delete iks;
	    }
	}
	
	for (std::size_t o = 0 ; o < constr.orientation_constraints.size() ; ++o)
	{
	    boost::shared_ptr<OrientationConstraint> oc(new OrientationConstraint(kmodel, ftf));
	    if (oc->use(constr.orientation_constraints[o]))
	    {
		IKConstraintSampler *iks = new IKConstraintSampler(jmg, ik_alloc, IKSamplingPose(oc));
		if (iks->initialize())
		{
		    bool use = true;
		    if (usedL.find(constr.orientation_constraints[o].link_name) != usedL.end())
			if (usedL[constr.orientation_constraints[o].link_name]->getSamplingVolume() < iks->getSamplingVolume())
			    use = false;
		    if (use)
		    {
			usedL[constr.orientation_constraints[o].link_name] = iks;
			ROS_DEBUG("Allocated an IK-based sampler for group '%s' satisfying orientation constraints on link '%s'", 
				  jmg->getName().c_str(), constr.orientation_constraints[o].link_name.c_str());
		    } 
		    else
			delete iks;
		} 
		else
		    delete iks;
	    }
	}

	if (usedL.size() == 1)
	    return ConstraintSamplerPtr(usedL.begin()->second);
	
	if (usedL.size() > 1)
	{
	    ROS_DEBUG("Too many IK-based samplers for group '%s'. Keeping the one with minimal sampling volume", jmg->getName().c_str());
	    // find the sampler with the smallest sampling volume; delete the rest
	    IKConstraintSampler *iks = usedL.begin()->second;
	    double msv = iks->getSamplingVolume();
	    for (std::map<std::string, IKConstraintSampler*>::const_iterator it = ++usedL.begin() ; it != usedL.end() ; ++it)
	    {
		double v = it->second->getSamplingVolume();
		if (v < msv)
		{
		    ROS_DEBUG("Discarding IK-based sampler for link '%s'", iks->getLinkName().c_str());
		    delete iks;
		    iks = it->second;
		    msv = v;
		} 
		else
		{
		    ROS_DEBUG("Discarding IK-based sampler for link '%s'", it->first.c_str());
		    delete it->second;
		}
	    }
	    return ConstraintSamplerPtr(iks);
	}
    }
    
    // if we got to this point, we have not decided on a sampler.
    // we now check to see if we can use samplers from subgroups
    if (!ik_subgroup_alloc.ik_allocators_.empty())
    {	
	std::vector<ConstraintSamplerPtr> samplers;
	for (std::map<const planning_models::KinematicModel::JointModelGroup*, IKAllocator>::const_iterator it = ik_subgroup_alloc.ik_allocators_.begin() ;
	     it != ik_subgroup_alloc.ik_allocators_.end() ; ++it)
	{
	    moveit_msgs::Constraints sub_constr;
	    for (std::size_t p = 0 ; p < constr.position_constraints.size() ; ++p)
		if (it->first->hasLinkModel(constr.position_constraints[p].link_name))
		    sub_constr.position_constraints.push_back(constr.position_constraints[p]);
	    for (std::size_t o = 0 ; o < constr.orientation_constraints.size() ; ++o)
		if (it->first->hasLinkModel(constr.orientation_constraints[o].link_name))
		    sub_constr.orientation_constraints.push_back(constr.orientation_constraints[o]);
	    if (!sub_constr.orientation_constraints.empty() || !sub_constr.position_constraints.empty())
	    {
		ConstraintSamplerPtr cs = constructConstraintsSampler(it->first, sub_constr, kmodel, ftf, it->second);
		if (cs)
		    samplers.push_back(cs);
	    }
	}
	if (!samplers.empty())
	    return ConstraintSamplerPtr(new UnionConstraintSampler(jmg, samplers));
    }
    
    if (joint_sampler)
	return joint_sampler;
    
    ROS_DEBUG("No constraints sampler allocated for group '%s'", jmg->getName().c_str());

    return ConstraintSamplerPtr();
}
