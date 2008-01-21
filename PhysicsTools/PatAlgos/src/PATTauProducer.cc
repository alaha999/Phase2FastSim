//
// $Id: PATTauProducer.cc,v 1.2 2008/01/16 20:33:09 lowette Exp $
//

#include "PhysicsTools/PatAlgos/interface/PATTauProducer.h"

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/FileInPath.h"
#include "DataFormats/Common/interface/View.h"

#include "DataFormats/HepMCCandidate/interface/GenParticleCandidate.h"
#include "PhysicsTools/Utilities/interface/DeltaR.h"

#include <DataFormats/TauReco/interface/PFTau.h>
#include <DataFormats/TauReco/interface/PFTauDiscriminatorByIsolation.h>
#include <DataFormats/TauReco/interface/CaloTau.h>
#include <DataFormats/TauReco/interface/CaloTauDiscriminatorByIsolation.h>

#include "PhysicsTools/PatUtils/interface/ObjectResolutionCalc.h"
#include "PhysicsTools/PatUtils/interface/LeptonLRCalc.h"

#include <vector>
#include <memory>


using namespace pat;


PATTauProducer::PATTauProducer(const edm::ParameterSet & iConfig) {
  // initialize the configurables
  tauSrc_         = iConfig.getParameter<edm::InputTag>( "tauSource" );
  tauDiscSrc_     = iConfig.getParameter<edm::InputTag>( "tauDiscriminatorSource");
  addGenMatch_    = iConfig.getParameter<bool>         ( "addGenMatch" );
  addResolutions_ = iConfig.getParameter<bool>         ( "addResolutions" );
  useNNReso_      = iConfig.getParameter<bool>         ( "useNNResolutions" );
  addLRValues_    = iConfig.getParameter<bool>         ( "addLRValues" );
  genPartSrc_     = iConfig.getParameter<edm::InputTag>( "genParticleSource" );
  tauResoFile_    = iConfig.getParameter<std::string>  ( "tauResoFile" );
  tauLRFile_      = iConfig.getParameter<std::string>  ( "tauLRFile" );

  // construct resolution calculator
  if (addResolutions_) {
    theResoCalc_ = new ObjectResolutionCalc(edm::FileInPath(tauResoFile_).fullPath(), useNNReso_);
  }

  // produces vector of taus
  produces<std::vector<Tau> >();
}


PATTauProducer::~PATTauProducer() {
  if (addResolutions_) delete theResoCalc_;
}


void PATTauProducer::produce(edm::Event & iEvent, const edm::EventSetup & iSetup) {     
 
  // Get the collection of taus from the event
  edm::Handle<PFTauCollection> PFtaus;
  edm::Handle<PFTauDiscriminatorByIsolation> PFtauIsolator;
  edm::Handle<CaloTauCollection> Calotaus; 
  edm::Handle<CaloTauDiscriminatorByIsolation> CalotauIsolator;
  bool hasPFtaus = false;
  bool hasCalotaus = false;
  try {
    iEvent.getByLabel(tauSrc_, PFtaus);
    iEvent.getByLabel(tauDiscSrc_, PFtauIsolator);
    hasPFtaus = true;
  } catch( const edm::Exception &roEX) { }
  try {
    iEvent.getByLabel(tauSrc_, Calotaus);
    iEvent.getByLabel(tauDiscSrc_, CalotauIsolator);
    hasCalotaus = true;
  } catch( const edm::Exception &roEX) { }
  if(!hasCalotaus && !hasPFtaus) {
    //Important note:
    // We are not issuing a LogError to be able to run on AOD samples
    // produced < 1_7_0, like CSA07 samples.
    // Note that missing input will not block je job.
    // In that case, an empty collection will be produced.
    edm::LogWarning("DataSource") << "WARNING! No Tau collection found. This missing input will not block the job. Instead, an empty tau collection is being be produced.";
  }
  if(hasCalotaus && hasPFtaus) {
    edm::LogError("DataSource") << "Ambiguous datasource. Taus can be both CaloTaus or PF taus.";
  }

  // Get the vector of generated particles from the event if needed
  edm::Handle<edm::View<reco::Candidate> > particles;
  if (addGenMatch_) {
    iEvent.getByLabel(genPartSrc_, particles);
  }

  // prepare LR calculation if required
  if (addLRValues_) {
    theLeptonLRCalc_ = new LeptonLRCalc(iSetup, "", "", edm::FileInPath(tauLRFile_).fullPath());
  }

  // collection of produced objects
  std::vector<Tau> * patTaus = new std::vector<Tau>(); 

  // loop over taus and prepare pat::Tau's
  if(hasPFtaus) {
    for (PFTauCollection::size_type iPFTau=0;iPFTau<PFtaus->size();iPFTau++) {
      // check the discriminant
      PFTauRef thePFTau(PFtaus,iPFTau);
      bool disc = (*PFtauIsolator)[thePFTau];
      if(!disc) continue;
      // construct the pat::Tau
      Tau aTau(*thePFTau);
      // set the additional variables
      const reco::PFJet *pfJet = dynamic_cast<const reco::PFJet*>(thePFTau->pfTauTagInfoRef()->pfjetRef().get());
      if(pfJet) {
        aTau.setEmEnergyFraction(pfJet->chargedEmEnergyFraction()+pfJet->neutralEmEnergyFraction());
        aTau.setEOverP(thePFTau->energy()/thePFTau->leadTrack()->p());
      }
      // add the tau to the vector of pat::Tau's
      patTaus->push_back(aTau);
    }
  } else if(hasCalotaus) {
    for (CaloTauCollection::size_type iCaloTau=0;iCaloTau<Calotaus->size();iCaloTau++) {
      // check the discriminant
      CaloTauRef theCaloTau(Calotaus,iCaloTau);
      bool disc = (*CalotauIsolator)[theCaloTau];
      if(!disc) continue;
      // construct the pat::Tau
      Tau aTau(*theCaloTau);
      // set the additional variables
      const reco::CaloJet *tauJet = dynamic_cast<const reco::CaloJet*>(theCaloTau->caloTauTagInfoRef()->calojetRef().get());
      if(tauJet) {
        aTau.setEmEnergyFraction(tauJet->emEnergyFraction());
        aTau.setEOverP(tauJet->energy()/theCaloTau->leadTrack()->p());
      }
      // add the tau to the vector of pat::Tau's
      patTaus->push_back(aTau);
    }
  }

  // loop on the resulting collection of taus, and set other informations
  for(std::vector<Tau>::iterator aTau = patTaus->begin();aTau<patTaus->end(); ++aTau) {
    // match to generated final state taus
    if (addGenMatch_) {
      // initialize best match as null
      reco::GenParticleCandidate bestGenTau(0, reco::Particle::LorentzVector(0, 0, 0, 0), reco::Particle::Point(0,0,0), 0, 0, true);
      float bestDR = 5.; // this is the upper limit on the candidate matching. 
      // find the closest generated tau. No charge cut is applied
      for (edm::View<reco::Candidate>::const_iterator itGenTau = particles->begin(); itGenTau != particles->end(); ++itGenTau) {
        reco::GenParticleCandidate aGenTau = *(dynamic_cast<reco::GenParticleCandidate *>(const_cast<reco::Candidate *>(&*itGenTau)));
        if (abs(aGenTau.pdgId())==15 && aGenTau.status()==2) {
	  float currDR = DeltaR<reco::Candidate>()(aGenTau, *aTau);
          if (currDR < bestDR) {
            bestGenTau = aGenTau;
            bestDR = currDR;
          }
        }
      }
      aTau->setGenLepton(bestGenTau);
    }
    // add resolution info if demanded
    if (addResolutions_) {
      (*theResoCalc_)(*aTau);
    }
    // add lepton LR info if requested
    if (addLRValues_) {
      theLeptonLRCalc_->calcLikelihood(*aTau, iEvent);
    }
  }

  // sort taus in pT
  std::sort(patTaus->begin(), patTaus->end(), pTTauComparator_);

  // put genEvt object in Event
  std::auto_ptr<std::vector<Tau> > pOutTau(patTaus);
  iEvent.put(pOutTau);

  // destroy the lepton LR calculator
  if (addLRValues_) delete theLeptonLRCalc_;

}

