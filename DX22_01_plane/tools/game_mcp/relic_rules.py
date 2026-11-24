"""GameTypes.h RelicType indices, as exposed by GameMcpInventoryState.cpp."""
from enum import IntEnum


class Relic(IntEnum):
    AllBallAttackUp = 0
    CollisionAttackUp = 1
    BankShot = 2
    EmergencyRepairKit = 3
    StandardBallScope = 4
    HeavyBallCore = 5
    PierceBallCharger = 6
    BounceBallSpring = 7
    AnchorBallChain = 8
    BountyList = 9
    ExpandedBallOffer = 10
