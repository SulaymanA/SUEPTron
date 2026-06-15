#ifndef SUEPTON_SUEPTON_SUEPEVENT_H
#define SUEPTON_SUEPTON_SUEPEVENT_H

#include <vector>
#include "fastjet/ClusterSequence.hh"
#include "Math/Vector3D.h"

class SuepEvent {
public:
    explicit SuepEvent(const std::vector<fastjet::PseudoJet>& particles);

    float getSphericitySystem(std::vector<fastjet::PseudoJet> system, fastjet::PseudoJet systemJet);
    float getSphericitySUEP();
    float getSphericityISR();
    float ptSUEP();
    float ptISR();
    int16_t getNConstituentsSUEP();
    int16_t getNConstituentsISR();
    int8_t  getNAK15jets();

    int16_t NConstituentsSUEP;
    int16_t NConstituentsISR;
    int8_t  NAK15jets;
    fastjet::PseudoJet SUEP;
    fastjet::PseudoJet ISR;
    std::vector<fastjet::PseudoJet> ConstituentsSUEP;
    std::vector<fastjet::PseudoJet> ConstituentsISR;
    ROOT::Math::XYZVector BetaSUEP;
};

#endif // SUEPTON_SUEPTON_SUEPEVENT_H
