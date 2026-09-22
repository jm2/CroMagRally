//
// cpu_driver.h
//
// Pedal and stuck-recovery decisions of the CPU car driver (DoCPUControl_Car).
// They only read simulation state and advance with the simulation dt, so every
// peer makes the same choices for network bots.
//

#pragma once

void UpdateCPUStuckCheck(PlayerInfoType *pinfo, const OGLPoint3D *coord, float dt);
Boolean CPUShouldBrakeForSkid(const PlayerInfoType *pinfo, Boolean onGround, float spinRate);
uint32_t CPUPedalControlBits(PlayerInfoType *pinfo, Boolean brake, Boolean giveGas, float dt);
