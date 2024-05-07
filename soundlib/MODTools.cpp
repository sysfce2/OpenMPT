/*
 * MODTools.cpp
 * ------------
 * Purpose: Definition of MOD file structures (shared between several SoundTracker-/ProTracker-like formats) and helper functions
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#include "stdafx.h"
#include "Loaders.h"
#include "MODTools.h"
#include "Tables.h"

OPENMPT_NAMESPACE_BEGIN

void CSoundFile::ConvertModCommand(ModCommand &m, const uint8 command, const uint8 param)
{
	m.param = param;
	switch(command)
	{
	case 0x00: m.command = m.param ? CMD_ARPEGGIO : CMD_NONE; break;
	case 0x01: m.command = CMD_PORTAMENTOUP; break;
	case 0x02: m.command = CMD_PORTAMENTODOWN; break;
	case 0x03: m.command = CMD_TONEPORTAMENTO; break;
	case 0x04: m.command = CMD_VIBRATO; break;
	case 0x05: m.command = CMD_TONEPORTAVOL; break;
	case 0x06: m.command = CMD_VIBRATOVOL; break;
	case 0x07: m.command = CMD_TREMOLO; break;
	case 0x08: m.command = CMD_PANNING8; break;
	case 0x09: m.command = CMD_OFFSET; break;
	case 0x0A: m.command = CMD_VOLUMESLIDE; break;
	case 0x0B: m.command = CMD_POSITIONJUMP; break;
	case 0x0C: m.command = CMD_VOLUME; break;
	case 0x0D: m.command = CMD_PATTERNBREAK; m.param = static_cast<ModCommand::PARAM>(((m.param >> 4) * 10) + (m.param & 0x0F)); break;
	case 0x0E: m.command = CMD_MODCMDEX; break;
	case 0x0F:
		// For a very long time, this code imported 0x20 as CMD_SPEED for MOD files, but this seems to contradict
		// pretty much the majority of other MOD player out there.
		// 0x20 is Speed: Impulse Tracker, Scream Tracker, old ModPlug
		// 0x20 is Tempo: ProTracker, XMPlay, Imago Orpheus, Cubic Player, ChibiTracker, BeRoTracker, DigiTrakker, DigiTrekker, Disorder Tracker 2, DMP, Extreme's Tracker, ...
		if(m.param < 0x20)
			m.command = CMD_SPEED;
		else
			m.command = CMD_TEMPO;
		break;

	// Extension for XM extended effects
	case 'G' - 55: m.command = CMD_GLOBALVOLUME; break;  //16
	case 'H' - 55: m.command = CMD_GLOBALVOLSLIDE; break;
	case 'K' - 55: m.command = CMD_KEYOFF; break;
	case 'L' - 55: m.command = CMD_SETENVPOSITION; break;
	case 'P' - 55: m.command = CMD_PANNINGSLIDE; break;
	case 'R' - 55: m.command = CMD_RETRIG; break;
	case 'T' - 55: m.command = CMD_TREMOR; break;
	case 'W' - 55: m.command = CMD_DUMMY; break;
	case 'X' - 55: m.command = CMD_XFINEPORTAUPDOWN;	break;
	case 'Y' - 55: m.command = CMD_PANBRELLO; break;   // 34
	case 'Z' - 55: m.command = CMD_MIDI; break;        // 35
	case '\\' - 56: m.command = CMD_SMOOTHMIDI; break;  // 36 - note: this is actually displayed as "-" in FT2, but seems to be doing nothing.
	case 37:        m.command = CMD_SMOOTHMIDI; break;  // BeRoTracker uses this for smooth MIDI macros for some reason; in old OpenMPT versions this was reserved for the unimplemented "velocity" command
	case '#' + 3:   m.command = CMD_XPARAM; break;      // 38
	default:        m.command = CMD_NONE;
	}
}

#ifndef MODPLUG_NO_FILESAVE

void CSoundFile::ModSaveCommand(const ModCommand &source, uint8 &command, uint8 &param, const bool toXM, const bool compatibilityExport) const
{
	command = 0;
	param = source.param;
	switch(source.command)
	{
	case CMD_NONE:		command = param = 0; break;
	case CMD_ARPEGGIO:	command = 0; break;
	case CMD_PORTAMENTOUP:
		if (GetType() & (MOD_TYPE_S3M|MOD_TYPE_IT|MOD_TYPE_STM|MOD_TYPE_MPT))
		{
			if ((param & 0xF0) == 0xE0) { command = 0x0E; param = ((param & 0x0F) >> 2) | 0x10; break; }
			else if ((param & 0xF0) == 0xF0) { command = 0x0E; param &= 0x0F; param |= 0x10; break; }
		}
		command = 0x01;
		break;
	case CMD_PORTAMENTODOWN:
		if(GetType() & (MOD_TYPE_S3M|MOD_TYPE_IT|MOD_TYPE_STM|MOD_TYPE_MPT))
		{
			if ((param & 0xF0) == 0xE0) { command = 0x0E; param= ((param & 0x0F) >> 2) | 0x20; break; }
			else if ((param & 0xF0) == 0xF0) { command = 0x0E; param &= 0x0F; param |= 0x20; break; }
		}
		command = 0x02;
		break;
	case CMD_TONEPORTAMENTO:	command = 0x03; break;
	case CMD_VIBRATO:			command = 0x04; break;
	case CMD_TONEPORTAVOL:		command = 0x05; break;
	case CMD_VIBRATOVOL:		command = 0x06; break;
	case CMD_TREMOLO:			command = 0x07; break;
	case CMD_PANNING8:
		command = 0x08;
		if(GetType() & MOD_TYPE_S3M)
		{
			if(param <= 0x80)
			{
				param = mpt::saturate_cast<uint8>(param * 2);
			}
			else if(param == 0xA4)	// surround
			{
				if(compatibilityExport || !toXM)
				{
					command = param = 0;
				}
				else
				{
					command = 'X' - 55;
					param = 91;
				}
			}
		}
		break;
	case CMD_OFFSET:			command = 0x09; break;
	case CMD_VOLUMESLIDE:		command = 0x0A; break;
	case CMD_POSITIONJUMP:		command = 0x0B; break;
	case CMD_VOLUME:			command = 0x0C; break;
	case CMD_PATTERNBREAK:		command = 0x0D; param = ((param / 10) << 4) | (param % 10); break;
	case CMD_MODCMDEX:			command = 0x0E; break;
	case CMD_SPEED:				command = 0x0F; param = std::min(param, uint8(0x1F)); break;
	case CMD_TEMPO:				command = 0x0F; param = std::max(param, uint8(0x20)); break;
	case CMD_GLOBALVOLUME:		command = 'G' - 55; break;
	case CMD_GLOBALVOLSLIDE:	command = 'H' - 55; break;
	case CMD_KEYOFF:			command = 'K' - 55; break;
	case CMD_SETENVPOSITION:	command = 'L' - 55; break;
	case CMD_PANNINGSLIDE:		command = 'P' - 55; break;
	case CMD_RETRIG:			command = 'R' - 55; break;
	case CMD_TREMOR:			command = 'T' - 55; break;
	case CMD_DUMMY:				command = 'W' - 55; break;
	case CMD_XFINEPORTAUPDOWN:	command = 'X' - 55;
		if(compatibilityExport && param >= 0x30)	// X1x and X2x are legit, everything above are MPT extensions, which don't belong here.
			param = 0;	// Don't set command to 0 to indicate that there *was* some X command here...
		break;
	case CMD_PANBRELLO:
		if(compatibilityExport)
			command = param = 0;
		else
			command = 'Y' - 55;
		break;
	case CMD_MIDI:
		if(compatibilityExport)
			command = param = 0;
		else
			command = 'Z' - 55;
		break;
	case CMD_SMOOTHMIDI: //rewbs.smoothVST: 36
		if(compatibilityExport)
			command = param = 0;
		else
			command = '\\' - 56;
		break;
	case CMD_XPARAM: //rewbs.XMfixes - XParam is 38
		if(compatibilityExport)
			command = param = 0;
		else
			command = '#' + 3;
		break;
	case CMD_S3MCMDEX:
		{
			ModCommand mConv;
			mConv.command = CMD_S3MCMDEX;
			mConv.param = param;
			mConv.ExtendedS3MtoMODEffect();
			ModSaveCommand(mConv, command, param, toXM, compatibilityExport);
		}
		return;
	case CMD_VOLUME8:
		command = 0x0C;
		param = static_cast<uint8>((param + 3u) / 4u);
		break;
	default:
		command = param = 0;
	}

	// Don't even think about saving XM effects in MODs...
	if(command > 0x0F && !toXM)
	{
		command = param = 0;
	}
}

#endif  // MODPLUG_NO_FILESAVE


// Convert an MOD sample header to OpenMPT's internal sample header.
void MODSampleHeader::ConvertToMPT(ModSample &mptSmp, bool is4Chn) const
{
	mptSmp.Initialize(MOD_TYPE_MOD);
	mptSmp.nLength = length * 2;
	mptSmp.nFineTune = MOD2XMFineTune(finetune & 0x0F);
	mptSmp.nVolume = 4u * std::min(volume.get(), uint8(64));

	SmpLength lStart = loopStart * 2;
	SmpLength lLength = loopLength * 2;
	// See if loop start is incorrect as words, but correct as bytes (like in Soundtracker modules)
	if(lLength > 2 && (lStart + lLength > mptSmp.nLength)
		&& (lStart / 2 + lLength <= mptSmp.nLength))
	{
		lStart /= 2;
	}

	if(mptSmp.nLength == 2)
	{
		mptSmp.nLength = 0;
	}

	if(mptSmp.nLength)
	{
		mptSmp.nLoopStart = lStart;
		mptSmp.nLoopEnd = lStart + lLength;

		if(mptSmp.nLoopStart >= mptSmp.nLength)
		{
			mptSmp.nLoopStart = mptSmp.nLength - 1;
		}
		if(mptSmp.nLoopStart > mptSmp.nLoopEnd || mptSmp.nLoopEnd < 4 || mptSmp.nLoopEnd - mptSmp.nLoopStart < 4)
		{
			mptSmp.nLoopStart = 0;
			mptSmp.nLoopEnd = 0;
		}

		// Fix for most likely broken sample loops. This fixes super_sufm_-_new_life.mod (M.K.) which has a long sample which is looped from 0 to 4.
		// This module also has notes outside of the Amiga frequency range, so we cannot say that it should be played using ProTracker one-shot loops.
		// On the other hand, "Crew Generation" by Necros (6CHN) has a sample with a similar loop, which is supposed to be played.
		// To be able to correctly play both modules, we will draw a somewhat arbitrary line here and trust the loop points in MODs with more than
		// 4 channels, even if they are tiny and at the very beginning of the sample.
		if(mptSmp.nLoopEnd <= 8 && mptSmp.nLoopStart == 0 && mptSmp.nLength > mptSmp.nLoopEnd && is4Chn)
		{
			mptSmp.nLoopEnd = 0;
		}
		if(mptSmp.nLoopEnd > mptSmp.nLoopStart)
		{
			mptSmp.uFlags.set(CHN_LOOP);
		}
	}
}


// Convert OpenMPT's internal sample header to a MOD sample header.
SmpLength MODSampleHeader::ConvertToMOD(const ModSample &mptSmp)
{
	SmpLength writeLength = mptSmp.HasSampleData() ? mptSmp.nLength : 0;
	// If the sample size is odd, we have to add a padding byte, as all sample sizes in MODs are even.
	if((writeLength % 2u) != 0)
	{
		writeLength++;
	}
	LimitMax(writeLength, SmpLength(0x1FFFE));

	length = static_cast<uint16>(writeLength / 2u);

	if(mptSmp.RelativeTone < 0)
	{
		finetune = 0x08;
	} else if(mptSmp.RelativeTone > 0)
	{
		finetune = 0x07;
	} else
	{
		finetune = XM2MODFineTune(mptSmp.nFineTune);
	}
	volume = static_cast<uint8>(mptSmp.nVolume / 4u);

	loopStart = 0;
	loopLength = 1;
	if(mptSmp.uFlags[CHN_LOOP] && (mptSmp.nLoopStart + 2u) < writeLength)
	{
		const SmpLength loopEnd = Clamp(mptSmp.nLoopEnd, (mptSmp.nLoopStart & ~1) + 2u, writeLength) & ~1;
		loopStart = static_cast<uint16>(mptSmp.nLoopStart / 2u);
		loopLength = static_cast<uint16>((loopEnd - (mptSmp.nLoopStart & ~1)) / 2u);
	}

	return writeLength;
}


// Compute a "rating" of this sample header by counting invalid header data to ultimately reject garbage files.
uint32 MODSampleHeader::GetInvalidByteScore() const
{
	return ((volume > 64) ? 1 : 0)
		    + ((finetune > 15) ? 1 : 0)
		    + ((loopStart > length * 2) ? 1 : 0);
}


bool MODSampleHeader::HasDiskName() const
{
	return (!memcmp(name, "st-", 3) || !memcmp(name, "ST-", 3)) && name[5] == ':';
}


// Count malformed bytes in MOD pattern data
uint32 CountMalformedMODPatternData(const MODPatternData &patternData, const bool extendedFormat)
{
	const uint8 mask = extendedFormat ? 0xE0 : 0xF0;
	uint32 malformedBytes = 0;
	for(const auto &row : patternData)
	{
		for(const auto &data : row)
		{
			if(data[0] & mask)
				malformedBytes++;
			if(!extendedFormat)
			{
				const uint16 period = (((static_cast<uint16>(data[0]) & 0x0F) << 8) | data[1]);
				if(period && period != 0xFFF)
				{
					// Allow periods to deviate by +/-1 as found in some files
					const auto CompareFunc = [](uint16 l, uint16 r) { return l > (r + 1); };
					const auto PeriodTable = mpt::as_span(ProTrackerPeriodTable).subspan(24, 36);
					if(!std::binary_search(PeriodTable.begin(), PeriodTable.end(), period, CompareFunc))
						malformedBytes += 2;
				}
			}
		}
	}
	return malformedBytes;
}


// Parse the order list to determine how many patterns are used in the file.
PATTERNINDEX GetNumPatterns(FileReader &file, ModSequence &Order, ORDERINDEX numOrders, SmpLength totalSampleLen, CHANNELINDEX &numChannels, SmpLength wowSampleLen, bool validateHiddenPatterns)
{
	PATTERNINDEX numPatterns = 0;         // Total number of patterns in file (determined by going through the whole order list) with pattern number < 128
	PATTERNINDEX officialPatterns = 0;    // Number of patterns only found in the "official" part of the order list (i.e. order positions < claimed order length)
	PATTERNINDEX numPatternsIllegal = 0;  // Total number of patterns in file, also counting in "invalid" pattern indexes >= 128

	for(ORDERINDEX ord = 0; ord < 128; ord++)
	{
		PATTERNINDEX pat = Order[ord];
		if(pat < 128 && numPatterns <= pat)
		{
			numPatterns = pat + 1;
			if(ord < numOrders)
			{
				officialPatterns = numPatterns;
			}
		}
		if(pat >= numPatternsIllegal)
		{
			numPatternsIllegal = pat + 1;
		}
	}

	// Remove the garbage patterns past the official order end now that we don't need them anymore.
	Order.resize(numOrders);

	const size_t patternStartOffset = file.GetPosition();
	const size_t sizeWithoutPatterns = totalSampleLen + patternStartOffset;
	const size_t sizeWithOfficialPatterns = sizeWithoutPatterns + officialPatterns * numChannels * 256;

	if(wowSampleLen && (wowSampleLen + patternStartOffset) + numPatterns * 8 * 256 == (file.GetLength() & ~1))
	{
		// Check if this is a Mod's Grave WOW file... WOW files use the M.K. magic but are actually 8CHN files.
		// We do a simple pattern validation as well for regular MOD files that have non-module data attached at the end
		// (e.g. ponylips.mod, MD5 c039af363b1d99a492dafc5b5f9dd949, SHA1 1bee1941c47bc6f913735ce0cf1880b248b8fc93)
		file.Seek(patternStartOffset + numPatterns * 4 * 256);
		if(ValidateMODPatternData(file, 16, true))
			numChannels = 8;
		file.Seek(patternStartOffset);
	} else if(numPatterns != officialPatterns && (validateHiddenPatterns || sizeWithOfficialPatterns == file.GetLength()))
	{
		// 15-sample SoundTracker specifics:
		// Fix SoundTracker modules where "hidden" patterns should be ignored.
		// razor-1911.mod (MD5 b75f0f471b0ae400185585ca05bf7fe8, SHA1 4de31af234229faec00f1e85e1e8f78f405d454b)
		// and captain_fizz.mod (MD5 55bd89fe5a8e345df65438dbfc2df94e, SHA1 9e0e8b7dc67939885435ea8d3ff4be7704207a43)
		// seem to have the "correct" file size when only taking the "official" patterns into account,
		// but they only play correctly when also loading the inofficial patterns.
		// On the other hand, the SoundTracker module
		// wolf1.mod (MD5 a4983d7a432d324ce8261b019257f4ed, SHA1 aa6b399d02546bcb6baf9ec56a8081730dea3f44),
		// wolf3.mod (MD5 af60840815aa9eef43820a7a04417fa6, SHA1 24d6c2e38894f78f6c5c6a4b693a016af8fa037b)
		// and jean_baudlot_-_bad_dudes_vs_dragonninja-dragonf.mod (MD5 fa48e0f805b36bdc1833f6b82d22d936, SHA1 39f2f8319f4847fe928b9d88eee19d79310b9f91)
		// only play correctly if we ignore the hidden patterns.
		// Hence, we have a peek at the first hidden pattern and check if it contains a lot of illegal data.
		// If that is the case, we assume it's part of the sample data and only consider the "official" patterns.

		// 31-sample NoiseTracker / ProTracker specifics:
		// Interestingly, (broken) variants of the ProTracker modules
		// "killing butterfly" (MD5 bd676358b1dbb40d40f25435e845cf6b, SHA1 9df4ae21214ff753802756b616a0cafaeced8021),
		// "quartex" by Reflex (MD5 35526bef0fb21cb96394838d94c14bab, SHA1 116756c68c7b6598dcfbad75a043477fcc54c96c),
		// seem to have the "correct" file size when only taking the "official" patterns into account, but they only play
		// correctly when also loading the inofficial patterns.
		// On the other hand, "Shofixti Ditty.mod" from Star Control 2 (MD5 62b7b0819123400e4d5a7813eef7fc7d, SHA1 8330cd595c61f51c37a3b6f2a8559cf3fcaaa6e8)
		// doesn't sound correct when taking the second "inofficial" pattern into account.
		file.Seek(patternStartOffset + officialPatterns * numChannels * 256);
		if(!ValidateMODPatternData(file, 64, true))
			numPatterns = officialPatterns;
		file.Seek(patternStartOffset);
	}

	if(numPatternsIllegal > numPatterns && sizeWithoutPatterns + numPatternsIllegal * numChannels * 256 == file.GetLength())
	{
		// Even those illegal pattern indexes (> 128) appear to be valid... What a weird file!
		// e.g. NIETNU.MOD, where the end of the order list is filled with FF rather than 00, and the file actually contains 256 patterns.
		numPatterns = numPatternsIllegal;
	} else if(numPatternsIllegal >= 0xFF)
	{
		// Patterns FE and FF are used with S3M semantics (e.g. some MODs written with old OpenMPT versions)
		Order.Replace(0xFE, Order.GetIgnoreIndex());
		Order.Replace(0xFF, Order.GetInvalidPatIndex());
	}

	return numPatterns;
}


std::pair<uint8, uint8> CSoundFile::ReadMODPatternEntry(FileReader &file, ModCommand &m)
{
	return ReadMODPatternEntry(file.ReadArray<uint8, 4>(), m);
}


std::pair<uint8, uint8> CSoundFile::ReadMODPatternEntry(const std::array<uint8, 4> data, ModCommand &m)
{
	// Read Period
	uint16 period = (((static_cast<uint16>(data[0]) & 0x0F) << 8) | data[1]);
	size_t note = NOTE_NONE;
	if(period > 0 && period != 0xFFF)
	{
		note = std::size(ProTrackerPeriodTable) + 23 + NOTE_MIN;
		for(size_t i = 0; i < std::size(ProTrackerPeriodTable); i++)
		{
			if(period >= ProTrackerPeriodTable[i])
			{
				if(period != ProTrackerPeriodTable[i] && i != 0)
				{
					uint16 p1 = ProTrackerPeriodTable[i - 1];
					uint16 p2 = ProTrackerPeriodTable[i];
					if(p1 - period < (period - p2))
					{
						note = i + 23 + NOTE_MIN;
						break;
					}
				}
				note = i + 24 + NOTE_MIN;
				break;
			}
		}
	}
	m.note = static_cast<ModCommand::NOTE>(note);
	// Read Instrument
	m.instr = (data[2] >> 4) | (data[0] & 0x10);
	// Read Effect
	m.command = CMD_NONE;
	uint8 command = data[2] & 0x0F, param = data[3];
	return {command, param};
}

OPENMPT_NAMESPACE_END
