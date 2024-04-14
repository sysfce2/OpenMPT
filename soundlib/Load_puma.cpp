/*
 * Load_puma.cpp
 * -------------
 * Purpose: PumaTracker module loader
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Loaders.h"
#include "InstrumentSynth.h"

OPENMPT_NAMESPACE_BEGIN

struct PumaPlaylistEntry
{
	struct Entry
	{
		uint8 pattern;
		int8 instrTranspose;
		int8 noteTranspose;
	};

	std::array<Entry, 4> channels;
	uint8 speed;
	uint8 zero;

	bool IsValid() const
	{
		for(const auto &chn : channels)
		{
			if(chn.pattern >= 128)
				return false;
			if((chn.noteTranspose & 1) || (chn.noteTranspose < -48 || chn.noteTranspose > 48))
				return false;
		}
		return speed <= 15 && !zero;
	}
};

MPT_BINARY_STRUCT(PumaPlaylistEntry, 14)


struct PumaFileHeader
{
	char     songName[12];
	uint16be lastOrder;  // "LoopTrack" in GUI
	uint16be numPatterns;
	uint16be numInstruments;
	uint16be unknown;
	uint32be sampleOffset[10];
	uint16be sampleLength[10];  // in words

	bool IsValid() const
	{
		for(int8 c : songName)
		{
			if(c != 0 && c < ' ')
				return false;
		}

		if(lastOrder > 255 || !numPatterns || numPatterns > 128 || !numInstruments || numInstruments > 32 || unknown)
			return false;

		const auto minSampleOffset = sizeof(PumaFileHeader) + GetHeaderMinimumAdditionalSize();
		for(uint32 i = 0; i < 10; i++)
		{
			if(sampleLength[i] > 0 && !sampleOffset[i])
				return false;
			if(sampleOffset[i] > 0x10'0000)
				return false;
			if(sampleOffset[i] > 0 && sampleOffset[i] < minSampleOffset)
				return false;
		}
		return true;
	}

	ORDERINDEX NumOrders() const
	{
		return lastOrder + 1;
	}

	uint32 GetHeaderMinimumAdditionalSize() const
	{
		return NumOrders() * static_cast<uint32>(sizeof(PumaPlaylistEntry)) + numPatterns * 8 + 4 + numInstruments * 16 + 4;
	}
};

MPT_BINARY_STRUCT(PumaFileHeader, 80)


static bool TranslatePumaScript(InstrumentSynth::Events &events, ModInstrument &instr, FileReader &file, bool isVolume)
{
	bool isFirst = true;
	while(file.CanRead(4))
	{
		const auto data = file.ReadArray<uint8, 4>();
		if(isFirst && isVolume && data[0] != 0xC0)
			return false;
		switch(data[0])
		{
		case 0xA0:  // Volume ramp / pitch ramp
			if(isVolume)
				events.push_back(InstrumentSynth::Event::Puma_VolumeRamp(std::min(data[1], uint8(64)), std::min(data[2], uint8(64)), data[3]));
			else
				events.push_back(InstrumentSynth::Event::Puma_PitchRamp(data[1], data[2], data[3]));
			break;
		case 0xB0:  // Jump
			if(data[1] & 3)
				return false;
			events.push_back(InstrumentSynth::Event::Jump(data[1] / 4u));
			return true;
		case 0xC0:  // Set waveform / -
			if(isVolume)
				events.push_back(InstrumentSynth::Event::Puma_SetWaveform(data[1], data[2], data[3]));
			else
				return false;
			if(isFirst)
				instr.AssignSample(data[1] + 1);
			break;
		case  0xD0:  // - / Set pitch
			// Odd values can be entered in the editor but playback will freeze
			if((data[1] & 1) || isVolume)
				return false;
			events.push_back(InstrumentSynth::Event::Puma_SetPitch(data[1], data[3]));
			break;
		case 0xE0:  // Stop sound / End of script
			if(isVolume)
				events.push_back(InstrumentSynth::Event::Puma_StopVoice());
			else
				events.push_back(InstrumentSynth::Event::StopScript());
			return true;
		default:
			if(!isVolume && !memcmp(data.data(), "inst", 4))
			{
				// vimto-02.puma has insf chunks that only consist of a single D0 00 00 01
				file.SkipBack(4);
				return !events.empty();
			}
			return false;
		}
		isFirst = false;
	}
	return !events.empty();
}


CSoundFile::ProbeResult CSoundFile::ProbeFileHeaderPuma(MemoryFileReader file, const uint64 *pfilesize)
{
	PumaFileHeader fileHeader;
	if(!file.ReadStruct(fileHeader))
		return ProbeWantMoreData;
	if(!fileHeader.IsValid())
		return ProbeFailure;

	const size_t probeOrders = std::min(static_cast<size_t>(fileHeader.NumOrders()), (ProbeRecommendedSize - sizeof(fileHeader)) / sizeof(PumaPlaylistEntry));
	for(size_t ord = 0; ord < probeOrders; ord++)
	{
		PumaPlaylistEntry entry;
		if(!file.ReadStruct(entry))
			return ProbeWantMoreData;
		if(!entry.IsValid())
			return ProbeFailure;
	}
	return ProbeAdditionalSize(file, pfilesize, fileHeader.GetHeaderMinimumAdditionalSize() - probeOrders * sizeof(PumaPlaylistEntry));
}


bool CSoundFile::ReadPuma(FileReader &file, ModLoadingFlags loadFlags)
{
	PumaFileHeader fileHeader;

	file.Rewind();
	if(!file.ReadStruct(fileHeader) || !fileHeader.IsValid())
		return false;
	if(!file.CanRead(fileHeader.GetHeaderMinimumAdditionalSize()))
		return false;
	if(loadFlags == onlyVerifyHeader)
		return true;

	InitializeGlobals(MOD_TYPE_MOD);
	InitializeChannels();
	SetupMODPanning(true);
	m_SongFlags.set(SONG_IMPORTED | SONG_ISAMIGA | SONG_FASTPORTAS);
	m_nChannels = 4;
	m_nSamples = 52;
	m_nInstruments = fileHeader.numInstruments;
	m_playBehaviour.set(kMODSampleSwap);

	m_songName = mpt::String::ReadBuf(mpt::String::maybeNullTerminated, fileHeader.songName);

	const ORDERINDEX numOrders = fileHeader.NumOrders();
	std::vector<PumaPlaylistEntry> orderData;
	file.ReadVector(orderData, numOrders);

	static constexpr ROWINDEX numRows = 32;
	std::vector<std::array<std::array<uint8, 3>, numRows>> patternData(fileHeader.numPatterns);
	for(auto &pattern : patternData)
	{
		if(!file.ReadMagic("patt"))
			return false;

		size_t row = 0;
		while(row < pattern.size() && file.CanRead(4))
		{
			const auto data = file.ReadArray<uint8, 4>();
			if(data[0] & 1)
				return false;
			if(!data[3] || data[3] > pattern.size() - row)
				return false;
			pattern[row] = {data[0], data[1], data[2]};
			row += data[3];
		}
	}
	if(!file.ReadMagic("patt"))
		return false;

	Order().resize(numOrders);
	for(ORDERINDEX ord = 0; ord < numOrders; ord++)
	{
		const PATTERNINDEX pat = Patterns.InsertAny(numRows);
		if(pat == PATTERNINDEX_INVALID)
			return false;
		if(!orderData[ord].IsValid())
			return false;
		Order()[ord] = pat;
		for(CHANNELINDEX chn = 0; chn < 4; chn++)
		{
			const auto &chnInfo = orderData[ord].channels[chn];
			if(chnInfo.pattern >= patternData.size())
				continue;
			ModCommand *m = Patterns[pat].GetpModCommand(0, chn);
			// Auto-portmentos appear to stop on pattern transitions and revert to the note's original pitch.
			VolumeCommand autoPorta = VOLCMD_NONE;
			for(const auto &p : patternData[chnInfo.pattern])
			{
				if(p[0])
					m->note = static_cast<uint8>(NOTE_MIDDLEC - 49 + (p[0] + chnInfo.noteTranspose) / 2);
				if(uint8 instr = (p[1] & 0x1F); instr != 0)
					m->instr = (instr + chnInfo.instrTranspose) & 0x1F;
				if(!m->instr && m->note != NOTE_NONE)
					m->instr = 99;
				m->param = p[2];
				switch(p[1] >> 5)
				{
				case 1:
					m->command = CMD_VOLUME;
					break;
				case 2:
					autoPorta = m->param ? VOLCMD_PORTADOWN : VOLCMD_NONE;
					if(autoPorta != VOLCMD_NONE)
						m->command = CMD_PORTAMENTODOWN;
					break;
				case 3:
					autoPorta = m->param ? VOLCMD_PORTAUP : VOLCMD_NONE;
					if(autoPorta != VOLCMD_NONE)
						m->command = CMD_PORTAMENTOUP;
					break;
				}
				if(m->command != CMD_PORTAMENTOUP && m->command != CMD_PORTAMENTODOWN)
				{
					if(m->note != NOTE_NONE)
						autoPorta = VOLCMD_NONE;
					else if(autoPorta != VOLCMD_NONE)
						m->volcmd = autoPorta;
				}
				m += m_nChannels;
			}
		}
		if(orderData[ord].speed)
			Patterns[pat].WriteEffect(EffectWriter(CMD_SPEED, orderData[ord].speed).RetryNextRow());
	}

	for(INSTRUMENTINDEX ins = 1; ins <= m_nInstruments; ins++)
	{
		if(!file.ReadMagic("inst"))
			return false;

		ModInstrument *instr = AllocateInstrument(ins);
		if(!instr)
			return false;

		instr->synth.m_scripts.resize(2);
		if(!TranslatePumaScript(instr->synth.m_scripts[0], *instr, file, true))
			return false;
		if(!file.ReadMagic("insf"))
			return false;
		if(!TranslatePumaScript(instr->synth.m_scripts[1], *instr, file, false))
			return false;
	}
	if(!file.ReadMagic("inst"))
		return false;

	SampleIO sampleIO(SampleIO::_8bit, SampleIO::mono, SampleIO::bigEndian, SampleIO::signedPCM);
	for(SAMPLEINDEX smp = 0; smp < 10; smp++)
	{
		if(!fileHeader.sampleLength[smp])
			continue;
		if(!file.Seek(fileHeader.sampleOffset[smp]))
			return false;
		ModSample &mptSmp = Samples[smp + 1];
		mptSmp.Initialize(MOD_TYPE_MOD);
		mptSmp.nLength = fileHeader.sampleLength[smp] * 2;
		sampleIO.ReadSample(mptSmp, file);
	}

	static constexpr std::array<uint8, 32> SampleData[] =
	{
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0x3F, 0x37, 0x2F, 0x27, 0x1F, 0x17, 0x0F, 0x07, 0xFF, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0x37, 0x2F, 0x27, 0x1F, 0x17, 0x0F, 0x07, 0xFF, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0x2F, 0x27, 0x1F, 0x17, 0x0F, 0x07, 0xFF, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0x27, 0x1F, 0x17, 0x0F, 0x07, 0xFF, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0x1F, 0x17, 0x0F, 0x07, 0xFF, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x17, 0x0F, 0x07, 0xFF, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x0F, 0x07, 0xFF, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x07, 0xFF, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x88, 0xFF, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x88, 0x80, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x90, 0x17, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x90, 0x98, 0x1F, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x90, 0x98, 0xA0, 0x27, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0x2F, 0x37},
		{0xC0, 0xC0, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0xF8, 0xF0, 0xE8, 0xE0, 0xD8, 0xD0, 0xC8, 0xC0, 0xB8, 0xB0, 0xA8, 0xA0, 0x98, 0x90, 0x88, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0x37},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7F, 0x7F, 0x7F},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F, 0x7F},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x80, 0x80, 0x80, 0x80, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x80, 0x80, 0x80, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x80, 0x80, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x80, 0x80, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F},
		{0x80, 0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x7F},
		{0x80, 0x80, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x45, 0x45, 0x79, 0x7D, 0x7A, 0x77, 0x70, 0x66, 0x61, 0x58, 0x53, 0x4D, 0x2C, 0x20, 0x18, 0x12},
		{0x04, 0xDB, 0xD3, 0xCD, 0xC6, 0xBC, 0xB5, 0xAE, 0xA8, 0xA3, 0x9D, 0x99, 0x93, 0x8E, 0x8B, 0x8A, 0x45, 0x45, 0x79, 0x7D, 0x7A, 0x77, 0x70, 0x66, 0x5B, 0x4B, 0x43, 0x37, 0x2C, 0x20, 0x18, 0x12},
		{0x04, 0xF8, 0xE8, 0xDB, 0xCF, 0xC6, 0xBE, 0xB0, 0xA8, 0xA4, 0x9E, 0x9A, 0x95, 0x94, 0x8D, 0x83, 0x00, 0x00, 0x40, 0x60, 0x7F, 0x60, 0x40, 0x20, 0x00, 0xE0, 0xC0, 0xA0, 0x80, 0xA0, 0xC0, 0xE0},
		{0x00, 0x00, 0x40, 0x60, 0x7F, 0x60, 0x40, 0x20, 0x00, 0xE0, 0xC0, 0xA0, 0x80, 0xA0, 0xC0, 0xE0, 0x80, 0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8},
		{0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x7F, 0x80, 0x80, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70},
	};
	for(SAMPLEINDEX smp = 0; smp < 42; smp++)
	{
		ModSample &mptSmp = Samples[smp + 11];
		mptSmp.Initialize(MOD_TYPE_MOD);
		mptSmp.nLength = 32;
		mptSmp.nLoopStart = 0;
		mptSmp.nLoopEnd = 32;
		mptSmp.uFlags.set(CHN_LOOP);
		FileReader smpFile{mpt::as_span(SampleData[smp])};
		sampleIO.ReadSample(mptSmp, smpFile);
	}

	m_modFormat.formatName = UL_("Puma Tracker");
	m_modFormat.type = UL_("puma");
	m_modFormat.madeWithTracker = UL_("Puma Tracker");
	m_modFormat.charset = mpt::Charset::Amiga_no_C1;

	return true;
}


OPENMPT_NAMESPACE_END
