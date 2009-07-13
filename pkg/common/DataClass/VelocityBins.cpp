// 2009 © Václav Šmilauer <eudoxos@arcig.cz>


#include"VelocityBins.hpp"
#include<yade/core/MetaBody.hpp>
#include<yade/pkg-common/RigidBodyParameters.hpp>
#include<boost/foreach.hpp>
#ifndef FOREACH
#	define FOREACH BOOST_FOREACH
#endif
CREATE_LOGGER(VelocityBins);

bool VelocityBins::incrementDists_shouldCollide(Real dt){
	int i=0;
	FOREACH(Bin& bin, bins){
		bin.currDistSq+=dt*dt*bin.currMaxVelSq; i++;
		if(bin.currDistSq>pow(bin.maxDist,2)){
			LOG_TRACE("Collide: bin"<<i<<": max dist "<<bin.maxDist<<", current "<<sqrt(bin.currDistSq));
			return true;
		}
	}
	return false;
}

void VelocityBins::setBins(MetaBody* rootBody, Real currMaxVelSq, Real refSweepLength){
	// initialization
		// sanity checks
		if(nBins<1 || nBins>100){LOG_FATAL("Number of bins must be >=1 and <=100"); abort(); }
		if(binOverlap>=1 || binOverlap<=0){ LOG_ERROR("binOverlap set to 0.8 (was "<<binOverlap<<", not in range (0…1) )"); binOverlap=0.8;}
		// number of bins changed
		if(nBins!=bins.size()){
			LOG_INFO("New number of bins: "<<nBins);
			bins.resize(nBins);	
		}
		// number of bodies changed
		if(bodyBins.size()!=rootBody->bodies->size()) bodyBins.resize(rootBody->bodies->size(),-1);
		// set the new overall refMaxVelSq
		if(refMaxVelSq<0){ refMaxVelSq=currMaxVelSq; /* first time */}
		else {
			// there should be some maximum speed change parameter, so that bins do not change their limits (and therefore bodies, also!) too often, depending on 1 particle going crazy
			refMaxVelSq=min(max(refMaxVelSq*pow(1-maxRefRelStep,2),currMaxVelSq),refMaxVelSq*pow(1+maxRefRelStep,2));
			if(refMaxVelSq==0) refMaxVelSq=currMaxVelSq;
		}
		LOG_TRACE("new refMaxVel: "<<sqrt(refMaxVelSq));
		// compute new minima/maxima for all bins, reset distance counters and real max velocity
		for(size_t i=0; i<nBins; i++){
			Bin& bin=bins[i];
			// 0th bin (fastest) has maximum the current maximum; slowest bin has minimum 0.
			bin.binMaxVelSq=(i==0       ? currMaxVelSq : refMaxVelSq/pow(binCoeff*binCoeff,i));
			bin.binMinVelSq=(i==nBins-1 ? 0.           : refMaxVelSq/pow(binCoeff*binCoeff,i+1));
			bin.maxDist=(i==0 ?
				(refMaxVelSq==0 ? 0: sqrt(currMaxVelSq/refMaxVelSq)*abs(refSweepLength)) :
				abs(refSweepLength)/pow(binCoeff,i)
			);
			bin.currDistSq=0; bin.currMaxVelSq=0; bin.nBodies=0;
		}
	long moveFaster=0, moveSlower=0;
	FOREACH(const shared_ptr<Body>& b, *rootBody->bodies){
		RigidBodyParameters* rbp=YADE_CAST<RigidBodyParameters*>(b->physicalParameters.get());
		Real velSq=VelocityBins::getBodyVelSq(rbp);
		binNo_t newBin=-1, oldBin=bodyBins[b->getId()];
		// we could compute logarithm and round it here, but perhaps this is faster
		for(size_t i=0; i<nBins; i++){
			// 1. do not allow to move up (slow down) more than one bin at time (reasonable?)
			// 2. for the current bin, put the lower margin lower by binOverlap^2, to avoid too many oscillations
			if((oldBin>=0 && oldBin==(binNo_t)(i-1)) || velSq>=bins[i].binMinVelSq*(oldBin==(binNo_t)i ? pow(binOverlap,2) : 1.) ){ newBin=(binNo_t)i; break;}
		}
		if(newBin<0){ LOG_FATAL("Body #"<<b->getId()<<", velSq="<<velSq<<" not put in any bin!?"); abort(); }
		if(oldBin>=0) { if(newBin>oldBin) moveSlower++; else if(oldBin>newBin) moveFaster++; }
		bodyBins[b->getId()]=newBin;
		// LOG_TRACE("#"<<b->getId()<<": vel="<<sqrt(velSq)<<", bin "<<(int)newBin);
		bins[newBin].nBodies+=1;
	}
	#ifdef LOG4CXX
		// if debugging output
		if(logger->isDebugEnabled() && (rootBody->currentIteration-histLast>=histInterval || histLast<0)){
			histLast=rootBody->currentIteration;
			LOG_INFO(bodyBins.size()<<" bodies (moves: "<<moveFaster<<" faster, "<<moveSlower<<" slower), refMaxVel="<<sqrt(refMaxVelSq));
			for(size_t i=0; i<nBins; i++){
				int nChars=int(80*(bins[i].nBodies/Real(bodyBins.size())));
				string l; for(int j=0; j<nChars; j++) l+="#"; for(int j=nChars; j<80; j++) l+=" ";
				cerr<<"\t"<<i<<": |"<<l<<"| ("<<bins[i].nBodies<<") "<<sqrt(bins[i].binMinVelSq)<<"…"<<sqrt(bins[i].binMaxVelSq)<<endl;
			}
		}
	#endif
}

/* non-parallel implementations */
void VelocityBins::binVelSqInitialize(){ FOREACH(Bin& bin, bins) bin.currMaxVelSq=0; }
void VelocityBins::binVelSqUse(body_id_t id, Real velSq){
	#ifdef YADE_OPENMP
		if(omp_get_thread_num()>0) {LOG_FATAL("VelocityBins do not support multiple openMP threads yet!"); abort(); }
	#endif
	Real& maxVelSq(bins[bodyBins[id]].currMaxVelSq);
	maxVelSq=max(maxVelSq,velSq);
}
void VelocityBins::binVelSqFinalize(){}
