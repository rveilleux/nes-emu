#pragma once

#include "Cpu.h"
#include "Ppu.h"
#include "Cartridge.h"
#include "Memory.h"
#include "CpuInternalRam.h"
#include "MemoryBus.h"
#include "FrameTimer.h"

class Nes
{
public:
	~Nes();

	void Initialize();
	
	RomHeader LoadRom(const char* file);
	void Reset();

	void ExecuteFrame(bool paused);

	void SetTurboEnabled(bool enabled) { m_turbo = enabled; }

	void SignalCpuNmi() { m_cpu.Nmi(); }
	void SignalCpuIrq() { m_cpu.Irq(); }

	float64 GetFps() const { return m_frameTimer.GetFps(); }
	NameTableMirroring GetNameTableMirroring() const { return m_cartridge.GetNameTableMirroring(); }
	void HACK_OnScanline() { m_cartridge.HACK_OnScanline(); }

private:
	friend class DebuggerImpl;

	void ExecuteCpuAndPpuFrame();

	FrameTimer m_frameTimer;
	Cpu m_cpu;
	Ppu m_ppu;
	Cartridge m_cartridge;
	CpuInternalRam m_cpuInternalRam;
	CpuMemoryBus m_cpuMemoryBus;
	PpuMemoryBus m_ppuMemoryBus;

	float64 m_lastSaveRamTime;
	bool m_turbo;
};
