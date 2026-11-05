#pragma once
#include <array>
namespace matrix_effect {
// A short settling window after focus/mode changes, never an idle poll.
class Recovery {
    unsigned next_=0;
public:
    void restart(){next_=0;}
    unsigned nextDelay(){constexpr std::array<unsigned,3> delays{250,750,1500};return next_<delays.size()?delays[next_++]:0;}
};
}
