#pragma once
#include "Base.h"
#include "Memory.h"
#include "Bitfield.h"
#include "FileStream.h"
#include <memory>

class Nes;

class Apu
{
public:
	Apu();
	~Apu();

	void Reset();
	uint8 HandleCpuRead(uint16 cpuAddress);
	void HandleCpuWrite(uint16 cpuAddress, uint8 value);
	void Execute(uint32 cpuCycles);
	void OutputFrame(bool paused); // Call when one frame has been rendered to emit one frame of new sound
	/*
	void Initialize(PpuMemoryBus& ppuMemoryBus, Nes& nes);

	void Execute(uint32 ppuCycles, bool& completedFrame);
	void RenderFrame(); // Call when Execute() sets completedFrame to true

	uint8 HandlePpuRead(uint16 ppuAddress);
	void HandlePpuWrite(uint16 ppuAddress, uint8 value);
	*/

private:
	uint32 m_frameCounter;
	uint32 m_cycleCounter;
	int m_frameSequencer;
	bool m_lastPause;
	FileStream m_debugFileDump;

	int16 m_pulseWaveFormDuty[4][8];
	int16 m_triangleWaveForm[32];
	uint16 m_noiseFreq[16];
	int16 m_noiseWaveForm[32768];
	uint8 m_noteLength[32];

	uint8 m_DMCEnabled;
	uint8 m_DMCLength;
	uint8 m_frameSequencerMode;
	uint8 m_frameSequencerIRQDisable;

	struct PulseData
	{
		uint8 m_pulseDuty;
		uint8 m_pulseHaltLoop;
		uint8 m_pulseConstant;
		uint8 m_pulseVolume;
		uint8 m_pulseSweepEnabled;
		uint8 m_pulseSweepPeriod;
		uint8 m_pulseSweepNeg;
		uint8 m_pulseSweepShift;
		bool m_pulseSweepReload;
		uint8 m_pulseSweepDivider;
		uint16 m_pulseSweepTargetPeriod;
		uint16 m_pulseTimer;
		uint8 m_pulseLength;
		uint32 m_pulseSequencer;
		uint8 m_pulseEnabled;
		uint8 m_pulseEnvelopeDivider;
		uint8 m_pulseEnvelopeCounter;
		bool m_startFlag;
	} m_pulses[2];

	uint8 m_triangleControlFlag;
	uint8 m_triangleCounterReload;
	uint16 m_triangleTimer;
	uint8 m_triangleLength;
	bool m_triangleReloadFlag;
	uint32 m_triangleSequencer;
	uint8 m_triangleEnabled;
	uint8 m_triangleCounter;

	uint8 m_noiseHaltFlag;
	uint8 m_noiseConstant;
	uint8 m_noiseVolume;
	uint8 m_noiseMode;
	uint8 m_noisePeriod;
	uint8 m_noiseLength;
	uint32 m_noiseSequencer;
	uint16 m_noiseLinearFeedback;
	uint8 m_noiseEnabled;
	bool m_noiseEnvelopeRestart;
	uint8 m_noiseEnvelopeDivider;
	uint8 m_noiseEnvelopeCounter;

	void OpenSDLAudio();
	void SetSDLAudioPause();
	static void StaticSDLAudioCallback(void *userdata, uint8 *stream, int len);
	void SDLAudioCallback(uint8 *stream, int len);
	void FillSoundBuffer(int16 *buffer, int len);
	void GenerateWaveForm();
	void ExecuteLengthAndSweep();
	void ExecuteEnvelope();
	/*
	uint16 MapCpuToPpuRegister(uint16 cpuAddress);
	uint16 MapPpuToVRam(uint16 ppuAddress);
	uint16 MapPpuToPalette(uint16 ppuAddress);

	uint8 ReadPpuRegister(uint16 cpuAddress);
	void WritePpuRegister(uint16 cpuAddress, uint8 value);

	void ClearBackground();
	void FetchBackgroundTileData();

	void ClearOAM2(); // OAM2 = $FF
	void PerformSpriteEvaluation(uint32 x, uint32 y); // OAM -> OAM2
	void FetchSpriteData(uint32 y); // OAM2 -> render (shift) registers

	void RenderPixel(uint32 x, uint32 y);
	void SetVBlankFlag();
	void OnFrameComplete();

	PpuMemoryBus* m_ppuMemoryBus;
	Nes* m_nes;
	*/
};
