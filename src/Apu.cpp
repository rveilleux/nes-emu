#include "Apu.h"
#include "Nes.h"
#include "Memory.h"
#include "Bitfield.h"
#include "MemoryMap.h"
#include "Debugger.h"
#include <tuple>
#include "SDL_audio.h"

/* Theory of operation: 
The NES CPU runs at 1.789773 MHz.
The APU runs at half that speed, so: 894886 KHz
This means that if we run at NTSC speed of 60 Hz, the APU will process 14914 cycles per frame
Since we are using SDL, sound quality 44100 Hz, 16-bit signed, we need to emit 735 16-bit samples per frame
*/

void Apu::StaticSDLAudioCallback(void *userdata, uint8 *stream, int len)
{
	((Apu*)userdata)->SDLAudioCallback(stream, len);
}

Apu::Apu()
{
	OpenSDLAudio();
	GenerateWaveForm();
	Reset();
	//m_debugFileDump.Open("audiodump-S16LSB.raw", "wb");
}

Apu::~Apu()
{
	m_debugFileDump.Close();
}

void Apu::OpenSDLAudio()
{
	SDL_AudioSpec desired, obtained;

	desired.freq = 44100;

	/* 16-bit signed audio */
	desired.format = AUDIO_S16LSB;

	desired.channels = 1;

	/* Large audio buffer reduces risk of dropouts but increases response time */
	// Use a power-of-two buffer length closest to 60hz (one frame) but smaller: 44100 / 60 = 735 so will will use 512
	desired.samples = 1024;

	desired.callback = StaticSDLAudioCallback;
	desired.userdata = this;

	/* Open the audio device */
	if (SDL_OpenAudio(&desired, &obtained) < 0)
	{
		fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
		exit(-1);
	}
	m_lastPause = false;
}

void Apu::SetSDLAudioPause()
{
	SDL_PauseAudio(m_lastPause);
}

void Apu::GenerateWaveForm()
{
	int16 amplitude = 70;
	for (int j = 0; j < 4; j++)
	{
		for (int i = 0; i < 8; i++)
		{
			m_pulseWaveFormDuty[j][i] = -amplitude;
		}
	}
	m_pulseWaveFormDuty[0][1] = amplitude;

	m_pulseWaveFormDuty[1][1] = amplitude;
	m_pulseWaveFormDuty[1][2] = amplitude;

	m_pulseWaveFormDuty[2][1] = amplitude;
	m_pulseWaveFormDuty[2][2] = amplitude;
	m_pulseWaveFormDuty[2][3] = amplitude;
	m_pulseWaveFormDuty[2][4] = amplitude;

	m_pulseWaveFormDuty[3][0] = amplitude;
	m_pulseWaveFormDuty[3][3] = amplitude;
	m_pulseWaveFormDuty[3][4] = amplitude;
	m_pulseWaveFormDuty[3][5] = amplitude;
	m_pulseWaveFormDuty[3][6] = amplitude;
	m_pulseWaveFormDuty[3][7] = amplitude;

	amplitude = 1400;
	for (int i = 0; i < 16; i++)
	{
		m_triangleWaveForm[i + 16] = (uint16) (i * 2 * amplitude / 16 - amplitude);
		m_triangleWaveForm[-i + 15] = m_triangleWaveForm[i + 16];
	}

	uint16 noisefreqs[] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
	memcpy(m_noiseFreq, noisefreqs, sizeof(m_noiseFreq));

	amplitude = 60;
	int feedback = 1;
	int bit = 0;
	for (int i = 0; i < 32768; i++)
	{
		m_noiseWaveForm[i] = (feedback & 1) * amplitude * 2 - amplitude;
		//bit = ((feedback & 1) ^ ((feedback & 64) >> 6));
		bit = ((feedback & 1) ^ ((feedback & 2) >> 1));
		feedback = (feedback >> 1) | (bit << 14);
	}

	uint8 notelength[] = { 10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14, 12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30 };
	memcpy(m_noteLength, notelength, sizeof(m_noteLength));
}

void Apu::Reset() {
	m_frameCounter = 0;
	for (int i = 0; i < 2; i++)
	{
		m_pulses[i].m_pulseTimer = 0;
		m_pulses[i].m_pulseEnabled = false;
		m_pulses[i].m_startFlag = false;
		m_pulses[i].m_pulseSweepReload = false;
		m_pulses[i].m_pulseSweepTargetPeriod = 0;
		m_pulses[i].m_pulseVolume = 0;
		m_pulses[i].m_pulseSequencer = 0;
	}
	m_triangleSequencer = 0;
	m_triangleTimer = 0;
	m_triangleEnabled = false;
	m_noiseSequencer = 0;
	m_noiseLinearFeedback = 1;
	m_noiseEnabled = false;
	m_noiseLength = 0;
	m_cycleCounter = 0;
	m_frameSequencer = 0;
	SetSDLAudioPause();
}

void Apu::SDLAudioCallback(uint8 *stream, int len)
{
	// Convert parameters to 16-bit since we are using "Signed 16-bit" audio
	int len16 = len >> 1;
	int16* buffer16 = (int16*)stream;
	FillSoundBuffer(buffer16, len16);
}

void Apu::FillSoundBuffer(int16 *buffer, int len)
{
	memset(buffer, 0, len << 1);
	// Mix in both pulse (rectangle / square waves)
	for (int p = 0; p < 2; p++)
	{
		if (!m_pulses[p].m_pulseEnabled || m_pulses[p].m_pulseLength == 0) continue;

		int freq = (5319481) / (16 * (m_pulses[p].m_pulseTimer + 1));

		//printf("frame %i, SDLAudioCallback len=%i\n", len);
		int volume = 0;
		if (m_pulses[p].m_pulseConstant)
		{
			volume = m_pulses[p].m_pulseVolume;
		} else {
			// volume controlled by enveloppe
			volume = m_pulses[p].m_pulseEnvelopeCounter;
		}
		if (m_pulses[p].m_pulseTimer < 8 || m_pulses[p].m_pulseSweepTargetPeriod > 0x7FF) // special case: if pulse timer is lower than 8, emit no sound for this channel
		{
			volume = 0;
		}
		if (volume == 0) continue;

		for (int i = 0; i < len; i++) {
			//buffer[i] = (int16)(sin((float)(i + m_frameCounter * len) / 44100 * M_PI * 2.f * freq) * volume);
			uint8 sequencer = (m_pulses[p].m_pulseSequencer >> 14) & 7;
			m_pulses[p].m_pulseSequencer += freq;
			buffer[i] += (int16) (m_pulseWaveFormDuty[m_pulses[p].m_pulseDuty][sequencer] * volume);
		}
	}
	
	
	{
		// The triangle channel is never silenced: it will keep emiting the same value forever when it is stopped.
		if (m_triangleEnabled && m_triangleLength != 0 && m_triangleCounter != 0 && m_triangleTimer > 1) {
			// Mix in triangle wave (bass)
			int freq = 21277924 / (32 * (m_triangleTimer + 1));
			for (int i = 0; i < len; i++) {
				uint8 sequencer = int(m_triangleSequencer >> 14) & 31;
				m_triangleSequencer += freq;
				buffer[i] += (int16)(m_triangleWaveForm[sequencer]);
			}
		} else {
			uint8 sequencer = int(m_triangleSequencer >> 14) & 31;
			int16 trianglevalue = m_triangleWaveForm[sequencer];
			for (int i = 0; i < len; i++) {
				buffer[i] += trianglevalue;
			}
		}
	}
	
	static int tweak = 10;

	if (m_noiseEnabled && m_noiseLength != 0)
	{
		// Mix in noise wave (drum and effects)
		int freq = 1789773 / (tweak * m_noiseFreq[m_noisePeriod]);
		int volume = 0;
		if (m_noiseConstant)
		{
			volume = m_noiseVolume;
		} else {
			// volume controlled by enveloppe
			volume = m_noiseEnvelopeCounter;
		}
		for (int i = 0; i < len; i++) {
			m_noiseSequencer += freq;
			buffer[i] += (int16)(m_noiseWaveForm[(m_noiseSequencer >> 14) & 32767] * volume);
		}
	}
	
	if (m_debugFileDump.IsOpen())
	{
		m_debugFileDump.Write(buffer, len);
	}
}

uint8 Apu::HandleCpuRead(uint16 cpuAddress)
{
	//printf("frame %i, HandleCpuRead, adress=%04X\n", frameCounter, cpuAddress);
	switch (cpuAddress)
	{
		// global flags:
	case CpuMemory::kApuControlStatus:
		uint8 value = 0;
		printf("Frame=%i, control status read from %x\n", m_frameCounter, value);
		return value;
	}
	return 0;
}

void Apu::HandleCpuWrite(uint16 cpuAddress, uint8 value)
{
	//printf("frame %i, HandleCpuWrite, adress=%04X = %i\n", frameCounter, cpuAddress, value);

	for (int p = 0; p < 2; p++)
	{
		uint16 pulseAddressOffset = uint16(p << 2);
		if (cpuAddress == CpuMemory::kApuPulse1ChannelA + pulseAddressOffset)
		{
			m_pulses[p].m_pulseDuty = (value & BITS(7, 6)) >> 6;
			m_pulses[p].m_pulseHaltLoop = (value & BITS(5)) >> 5;
			m_pulses[p].m_pulseConstant = (value & BITS(4)) >> 4;
			m_pulses[p].m_pulseVolume = (value & BITS(0, 1, 2, 3)) >> 0;
		}
		else if (cpuAddress == CpuMemory::kApuPulse1ChannelB + pulseAddressOffset)
		{
			m_pulses[p].m_pulseSweepEnabled = (value & BITS(7)) >> 7;
			m_pulses[p].m_pulseSweepPeriod = (value & BITS(6, 5, 4)) >> 4;
			m_pulses[p].m_pulseSweepNeg = (value & BITS(3)) >> 3;
			m_pulses[p].m_pulseSweepShift = (value & BITS(0, 1, 2)) >> 0;
			m_pulses[p].m_pulseSweepReload = true;
			//if (m_pulses[p].m_pulseSweepEnabled) {
			//	printf("  write adr: pulse %i, timer=%i enabling sweep=%i, divid=%i shift=%i\n", p, m_pulses[p].m_pulseTimer, m_pulses[p].m_pulseSweepEnabled, m_pulses[p].m_pulseSweepDivider, m_pulses[p].m_pulseSweepShift);
			//}
		}
		else if (cpuAddress == CpuMemory::kApuPulse1ChannelC + pulseAddressOffset)
		{
			m_pulses[p].m_pulseTimer = (m_pulses[p].m_pulseTimer & ~0xFF) | value;
		}
		else if (cpuAddress == CpuMemory::kApuPulse1ChannelD + pulseAddressOffset)
		{
			m_pulses[p].m_pulseLength = m_noteLength[(value & BITS(7, 6, 5, 4, 3)) >> 3];
			m_pulses[p].m_pulseTimer = (m_pulses[p].m_pulseTimer & ~0x700) | ((value & BITS(0, 1, 2)) << 8);
			m_pulses[p].m_startFlag = true;
		}
	}

	switch (cpuAddress)
	{
	// global flags:
	case CpuMemory::kApuControlStatus:
		m_DMCEnabled = (value & BITS(4)) >> 4;
		if (!m_DMCEnabled) m_DMCLength = 0;

		m_noiseEnabled = (value & BITS(3)) >> 3;
		if (!m_noiseEnabled) m_noiseLength = 0;

		m_triangleEnabled = (value & BITS(2)) >> 2;
		if (!m_triangleEnabled) m_triangleLength = 0;

		m_pulses[1].m_pulseEnabled = (value & BITS(1)) >> 1;
		if (!m_pulses[1].m_pulseEnabled) m_pulses[1].m_pulseLength = 0;

		m_pulses[0].m_pulseEnabled = (value & BITS(0)) >> 0;
		if (!m_pulses[0].m_pulseEnabled) m_pulses[0].m_pulseLength = 0;

		//printf("Frame=%i, control status set to %x\n", m_frameCounter, value & 31);
		break;
	case CpuMemory::kApuFrameCounter:
		m_frameSequencerMode = (value & BITS(7)) >> 7;
		m_frameSequencerIRQDisable = (value & BITS(6)) >> 6;
		//printf("Frame=%i, mode=%i, irq=%i\n", m_frameSequencerMode, m_frameSequencerIRQDisable);
		break;

	// triangle channel:
	case CpuMemory::kApuTriangleChannelA:
		m_triangleControlFlag = (value & BITS(7)) >> 7;
		m_triangleCounterReload = (value & ~BITS(7)) >> 0;
		break;
	case CpuMemory::kApuTriangleChannelB:
		m_triangleTimer = (m_triangleTimer & ~0xFF) | value;
		break;
	case CpuMemory::kApuTriangleChannelC:
		m_triangleLength = m_noteLength[(value & BITS(7, 6, 5, 4, 3)) >> 3];
		m_triangleTimer = (m_triangleTimer & ~0x700) | ((value & BITS(0, 1, 2)) << 8);
		m_triangleReloadFlag = true;
		break;

	// noise channel:
	case CpuMemory::kApuNoiseChannelA:
		m_noiseHaltFlag = (value & BITS(5)) >> 5;
		m_noiseConstant = (value & BITS(4)) >> 4;
		m_noiseVolume = (value & BITS(0, 1, 2, 3)) >> 0;
		break;
	case CpuMemory::kApuNoiseChannelB:
		m_noiseMode = (value & BITS(7)) >> 7;
		m_noisePeriod = (value & BITS(0, 1, 2, 3)) >> 0;
		//printf("frame=%i, noise mode=%i period=%i timerperiod=%i\n", m_frameCounter, m_noiseMode, m_noisePeriod, m_noiseSequencer);
		break;
	case CpuMemory::kApuNoiseChannelC:
		m_noiseLength = m_noteLength[(value & BITS(7, 6, 5, 4, 3)) >> 3];
		m_noiseEnvelopeRestart = true;
		break;
	}
}

void Apu::Execute(uint32 cpuCycles)
{
	// Simulate approximately 240Hz to perform various APU clocks
	// (take CPU speed / 7457)
	m_cycleCounter += cpuCycles;
	if (m_cycleCounter < 7457) return;
	m_cycleCounter -= 7457;

	switch (m_frameSequencerMode) {
	case 0: // mode 0: 4-step
		if (m_frameSequencer >= 4) {
			m_frameSequencer = 0;
		}
		if (m_frameSequencer == 1 || m_frameSequencer == 3) {
			ExecuteLengthAndSweep();
		}
		ExecuteEnvelope();
		break;
	case 1: // mode 1 : 5 - step
		if (m_frameSequencer >= 5) {
			m_frameSequencer = 0;
		}
		if (m_frameSequencer == 0 || m_frameSequencer == 2) {
			ExecuteLengthAndSweep();
		}
		if (m_frameSequencer <= 3) {
			ExecuteEnvelope();
		}
		break;
	}
	m_frameSequencer++;
}

void Apu::OutputFrame(bool paused)
{
	if (m_lastPause != paused)
	{
		m_lastPause = paused;
		SetSDLAudioPause();
	}
	if (paused) return;
	++m_frameCounter;
}

void Apu::ExecuteEnvelope() {
	// Update envelopes and triangle linear counter
	for (int p = 0; p < 2; p++)
	{
		if (m_pulses[p].m_startFlag) {
			m_pulses[p].m_startFlag = false;
			m_pulses[p].m_pulseEnvelopeCounter = 15;
			m_pulses[p].m_pulseEnvelopeDivider = m_pulses[p].m_pulseVolume + 1;
		} else {
			m_pulses[p].m_pulseEnvelopeDivider--;
			if (m_pulses[p].m_pulseEnvelopeDivider == 0) {
				m_pulses[p].m_pulseEnvelopeDivider = m_pulses[p].m_pulseVolume + 1;
				if (m_pulses[p].m_pulseEnvelopeCounter != 0) {
					m_pulses[p].m_pulseEnvelopeCounter--;
				} else {
					if (m_pulses[p].m_pulseHaltLoop) {
						m_pulses[p].m_pulseEnvelopeCounter = 15;
					}
				}
			}
		}
	}
	if (m_triangleReloadFlag) {
		m_triangleCounter = m_triangleCounterReload;
		if (m_triangleControlFlag == 0) {
			m_triangleReloadFlag = false;
		}
	} else {
		if (m_triangleCounter != 0) {
			m_triangleCounter--;
		}
	}
	if (m_noiseEnvelopeRestart) {
		m_noiseEnvelopeRestart = false;
		m_noiseEnvelopeCounter = 15;
		m_noiseEnvelopeDivider = m_noiseVolume + 1;
	} else {
		m_noiseEnvelopeDivider--;
		if (m_noiseEnvelopeDivider == 0) {
			m_noiseEnvelopeDivider = m_noiseVolume + 1;
			if (m_noiseEnvelopeCounter != 0) {
				m_noiseEnvelopeCounter--;
			} else {
				if (m_noiseHaltFlag) {
					m_noiseEnvelopeCounter = 15;
				}
			}
		}
	}
}

void Apu::ExecuteLengthAndSweep() {
	// Decrease note if 'Length counter' if enabled on any channels and update sweep units
	for (int p = 0; p < 2; p++)
	{
		if (m_pulses[p].m_pulseEnabled && m_pulses[p].m_pulseLength != 0 && m_pulses[p].m_pulseHaltLoop == 0) {
			m_pulses[p].m_pulseLength--;
		}
		bool reloadSweepPeriod = false;
		bool forceUpdatePulseTimer = false;
		if (m_pulses[p].m_pulseSweepReload)
		{
			if (m_pulses[p].m_pulseSweepDivider == 0) {
				forceUpdatePulseTimer = true;
			}
			reloadSweepPeriod = true;
			m_pulses[p].m_pulseSweepReload = false;
		} else {
			if (m_pulses[p].m_pulseSweepDivider != 0) {
				m_pulses[p].m_pulseSweepDivider--;
			} else {
				reloadSweepPeriod = true;
			}
		}
		if (reloadSweepPeriod) {
			if (m_pulses[p].m_pulseSweepEnabled) {
				m_pulses[p].m_pulseSweepDivider = m_pulses[p].m_pulseSweepPeriod + 1;
				int16 sweeping = m_pulses[p].m_pulseTimer >> m_pulses[p].m_pulseSweepShift;
				if (m_pulses[p].m_pulseSweepNeg) {
					sweeping = -sweeping;
					if (p == 0) sweeping--; // special case for pulse channel 0: -1
				}
				//printf("pulse %i, timer=%i enabling sweep=%i, divid=%i shift=%i sweeping=%i\n", p, m_pulses[p].m_pulseTimer, m_pulses[p].m_pulseSweepEnabled, m_pulses[p].m_pulseSweepDivider, m_pulses[p].m_pulseSweepShift, sweeping);
				m_pulses[p].m_pulseSweepTargetPeriod = m_pulses[p].m_pulseTimer + sweeping;
			}
		}
		if (m_pulses[p].m_pulseSweepEnabled && m_pulses[p].m_pulseSweepShift != 0 && (m_pulses[p].m_pulseSweepDivider == 0 || forceUpdatePulseTimer)) {
			if (!(m_pulses[p].m_pulseTimer < 8 || m_pulses[p].m_pulseSweepTargetPeriod > 0x7FF)) {
				m_pulses[p].m_pulseTimer = m_pulses[p].m_pulseSweepTargetPeriod;
			}
		}
	}
	if (m_triangleEnabled && m_triangleLength != 0 && m_triangleControlFlag == 0) {
		m_triangleLength--;
	}
	if (m_noiseEnabled && m_noiseLength != 0 && m_noiseHaltFlag == 0) {
		m_noiseLength--;
	}
}
