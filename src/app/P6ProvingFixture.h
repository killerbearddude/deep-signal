#pragma once

// Developer-only command-earned checkpoints for the bounded P6 proving loop.
// Starting geometry, teams, capacity and geology are authored facts; ships,
// observations, assessments, site hardware and supply are earned in Simulation.
#include "sim/GameState.h"

namespace deep {

struct P6EstablishedCheckpoints {
    GameState starting;
    GameState shipsBuilt;
    GameState observationAcquired;
    GameState assessmentPublished;
    GameState siteAuthorized;
    GameState sitePartiallyAssembled;
    GameState freightInTransit;
    GameState beforeFirstIceDelivery;
    GameState firstIceDelivery;
    GameState mature;
};

[[nodiscard]] GameState createP6EstablishedStartingWorld(bool usefulIce = true);
[[nodiscard]] P6EstablishedCheckpoints earnP6EstablishedLoop();

} // namespace deep
