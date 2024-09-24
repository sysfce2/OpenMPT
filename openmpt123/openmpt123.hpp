/*
 * openmpt123.hpp
 * --------------
 * Purpose: libopenmpt command line player
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */

#ifndef OPENMPT123_HPP
#define OPENMPT123_HPP

#include "openmpt123_config.hpp"

#include "mpt/base/compiletime_warning.hpp"
#include "mpt/base/detect.hpp"
#include "mpt/base/float.hpp"
#include "mpt/base/math.hpp"
#include "mpt/base/namespace.hpp"
#include "mpt/base/preprocessor.hpp"
#include "mpt/base/saturate_round.hpp"
#include "mpt/exception/exception_text.hpp"
#include "mpt/format/concat.hpp"
#include "mpt/format/message.hpp"
#include "mpt/format/message_macros.hpp"
#include "mpt/format/simple.hpp"
#include "mpt/io_file/fstream.hpp"
#include "mpt/parse/parse.hpp"
#include "mpt/path/native_path.hpp"
#include "mpt/string/types.hpp"
#include "mpt/string/utility.hpp"
#include "mpt/string_transcode/transcode.hpp"

#include <string>

#include <libopenmpt/libopenmpt.hpp>

namespace mpt {
inline namespace MPT_INLINE_NS {

template <>
struct make_string_type<mpt::native_path> {
	using type = mpt::native_path;
};


template <>
struct is_string_type<mpt::native_path> : public std::true_type { };

template <>
struct string_transcoder<mpt::native_path> {
	using string_type = mpt::native_path;
	static inline mpt::widestring decode( const string_type & src ) {
		return mpt::transcode<mpt::widestring>( src.AsNative() );
	}
	static inline string_type encode( const mpt::widestring & src ) {
		return mpt::native_path::FromNative( mpt::transcode<mpt::native_path::raw_path_type>( src ) );
	}
};

} // namespace MPT_INLINE_NS
} // namespace mpt

namespace openmpt123 {

struct exception : public openmpt::exception {
	exception( const mpt::ustring & text ) : openmpt::exception(mpt::transcode<std::string>( mpt::common_encoding::utf8, text )) { }
};

struct show_help_exception {
	mpt::ustring message;
	bool longhelp;
	show_help_exception( const mpt::ustring & msg = MPT_USTRING(""), bool longhelp_ = true ) : message(msg), longhelp(longhelp_) { }
};

struct args_nofiles_exception {
	args_nofiles_exception() = default;
};

struct args_error_exception {
	args_error_exception() = default;
};

struct show_help_keyboard_exception { };


template <typename Tstring, typename Tchar, typename T>
inline Tstring align_right( const Tchar pad, std::size_t width, const T val ) {
	assert( Tstring( 1, pad ).length() == 1 );
	Tstring str = mpt::default_formatter::template format<Tstring>( val );
	if ( width > str.length() ) {
		str.insert( str.begin(), width - str.length(), pad );
	}
	return str;
}

template <typename Tstring>
struct concat_stream {
	virtual concat_stream & append( Tstring str ) = 0;
	virtual ~concat_stream() = default;
	inline concat_stream<Tstring> & operator<<( concat_stream<Tstring> & (*func)( concat_stream<Tstring> & s ) ) {
		return func( *this );
	}
};

template <typename Tstring>
inline concat_stream<Tstring> & lf( concat_stream<Tstring> & s ) {
	return s.append( Tstring(1, mpt::char_constants<typename Tstring::value_type>::lf) );
}

template <typename T, typename Tstring>
inline concat_stream<Tstring> & operator<<( concat_stream<Tstring> & s, const T & val ) {
	return s.append( mpt::default_formatter::template format<Tstring>( val ) );
}

template <typename Tstring>
struct string_concat_stream
	: public concat_stream<Tstring>
{
private:
	Tstring m_str;
public:
	inline void str( Tstring s ) {
		m_str = std::move( s );
	}
	inline concat_stream<Tstring> & append( Tstring s ) override {
		m_str += std::move( s );
		return *this;
	}
	inline Tstring str() const {
		return m_str;
	}
	~string_concat_stream() override = default;
};


struct field {
	mpt::ustring key;
	mpt::ustring val;
};


#if MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)
inline bool IsConsole( DWORD stdHandle ) {
	HANDLE hStd = GetStdHandle( stdHandle );
	if ( ( hStd != NULL ) && ( hStd != INVALID_HANDLE_VALUE ) ) {
		DWORD mode = 0;
		if ( GetConsoleMode( hStd, &mode ) != FALSE ) {
			return true;
		}
	}
	return false;
}
#endif // MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)

inline bool IsTerminal( int fd ) {
#if MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)
	if ( !_isatty( fd ) ) {
		return false;
	}
	DWORD stdHandle = 0;
	if ( fd == 0 ) {
		stdHandle = STD_INPUT_HANDLE;
	} else if ( fd == 1 ) {
		stdHandle = STD_OUTPUT_HANDLE;
	} else if ( fd == 2 ) {
		stdHandle = STD_ERROR_HANDLE;
	}
	return IsConsole( stdHandle );
#else
	return isatty( fd ) ? true : false;
#endif
}


class textout : public string_concat_stream<mpt::ustring> {
protected:
	textout() = default;
public:
	virtual ~textout() = default;
protected:
	mpt::ustring pop() {
		mpt::ustring text = str();
		str( mpt::ustring() );
		return text;
	}
public:
	virtual void writeout() = 0;
	virtual void cursor_up( std::size_t lines ) = 0;
};



class textout_dummy : public textout {
public:
	textout_dummy() = default;
	~textout_dummy() override {
		static_cast<void>( pop() );
	}
public:
	void writeout() override {
		static_cast<void>( pop() );
	}
	void cursor_up( std::size_t lines ) override {
		static_cast<void>( lines );
	}
};



enum class textout_destination {
	destination_stdout,
	destination_stderr,
};

class textout_backend {
protected:
	textout_backend() = default;
public:
	virtual ~textout_backend() = default;
public:
	virtual void write( const mpt::ustring & text ) = 0;
	virtual void cursor_up(std::size_t lines) = 0;
};



class textout_ostream : public textout_backend {
private:
	std::ostream & s;
#if MPT_OS_DJGPP
	mpt::common_encoding codepage;
#endif
public:
	textout_ostream( std::ostream & s_ )
		: s(s_)
#if MPT_OS_DJGPP
		, codepage(mpt::common_encoding::cp437)
#endif
	{
		#if MPT_OS_DJGPP
			codepage = mpt::djgpp_get_locale_encoding();
		#endif
		return;
	}
	~textout_ostream() override = default;
public:
	void write( const mpt::ustring & text ) override {
		if ( text.length() > 0 ) {
			#if MPT_OS_DJGPP
				s << mpt::transcode<std::string>( codepage, text );
			#elif MPT_OS_EMSCRIPTEN
				s << mpt::transcode<std::string>( mpt::common_encoding::utf8, text ) ;
			#else
				s << mpt::transcode<std::string>( mpt::logical_encoding::locale, text );
			#endif
			s.flush();
		}	
	}
	void cursor_up( std::size_t lines ) override {
		s.flush();
		for ( std::size_t line = 0; line < lines; ++line ) {
			s << std::string("\x1b[1A");
		}
	}
};

#if MPT_OS_WINDOWS && defined(UNICODE)

class textout_wostream : public textout_backend {
private:
	std::wostream & s;
public:
	textout_wostream( std::wostream & s_ )
		: s(s_)
	{
		return;
	}
	~textout_wostream() override = default;
public:
	void write( const mpt::ustring & text ) override {
		if ( text.length() > 0 ) {
			s << mpt::transcode<std::wstring>( text );
			s.flush();
		}	
	}
	void cursor_up( std::size_t lines ) override {
		s.flush();
		for ( std::size_t line = 0; line < lines; ++line ) {
			s << std::wstring(L"\x1b[1A");
		}
	}
};

#endif // MPT_OS_WINDOWS && UNICODE

#if MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)

class textout_ostream_console : public textout_backend {
private:
#if defined(UNICODE)
	std::wostream & s;
#else
	std::ostream & s;
#endif
	HANDLE handle;
	bool console;
public:
#if defined(UNICODE)
	textout_ostream_console( std::wostream & s_, DWORD stdHandle_ )
#else
	textout_ostream_console( std::ostream & s_, DWORD stdHandle_ )
#endif
		: s(s_)
		, handle(GetStdHandle( stdHandle_ ))
		, console(IsConsole( stdHandle_ ))
	{
		return;
	}
	~textout_ostream_console() override = default;
public:
	void write( const mpt::ustring & text ) override {
		if ( text.length() > 0 ) {
			if ( console ) {
				DWORD chars_written = 0;
				#if defined(UNICODE)
					std::wstring wtext = mpt::transcode<std::wstring>( text );
					WriteConsole( handle, wtext.data(), static_cast<DWORD>( wtext.size() ), &chars_written, NULL );
				#else
					std::string ltext = mpt::transcode<std::string>( mpt::logical_encoding::locale, text );
					WriteConsole( handle, ltext.data(), static_cast<DWORD>( ltext.size() ), &chars_written, NULL );
				#endif
			} else {
				#if defined(UNICODE)
					s << mpt::transcode<std::wstring>( text );
				#else
					s << mpt::transcode<std::string>( mpt::logical_encoding::locale, text );
				#endif
				s.flush();
			}
		}
	}
	void cursor_up( std::size_t lines ) override {
		if ( console ) {
			s.flush();
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			ZeroMemory( &csbi, sizeof( CONSOLE_SCREEN_BUFFER_INFO ) );
			COORD coord_cursor = COORD();
			if ( GetConsoleScreenBufferInfo( handle, &csbi ) != FALSE ) {
				coord_cursor = csbi.dwCursorPosition;
				coord_cursor.X = 1;
				coord_cursor.Y -= static_cast<SHORT>( lines );
				SetConsoleCursorPosition( handle, coord_cursor );
			}
		}
	}
};

#endif // MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)



template <textout_destination dest>
class textout_wrapper : public textout {
private:
#if MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)
#if defined(UNICODE)
	textout_ostream_console out{ dest == textout_destination::destination_stdout ? std::wcout : std::wclog, dest == textout_destination::destination_stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE };
#else
	textout_ostream_console out{ dest == textout_destination::destination_stdout ? std::cout : std::clog, dest == textout_destination::destination_stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE };
#endif
#elif MPT_OS_WINDOWS
#if defined(UNICODE)
	textout_wostream out{ dest == textout_destination::destination_stdout ? std::wcout : std::wclog };
#else
	textout_ostream out{ dest == textout_destination::destination_stdout ? std::cout : std::clog };
#endif
#else
	textout_ostream out{ dest == textout_destination::destination_stdout ? std::cout : std::clog };
#endif
public:
	textout_wrapper() = default;
	~textout_wrapper() override {
		out.write( pop() );
	}
public:
	void writeout() override {
		out.write( pop() );
	}
	void cursor_up(std::size_t lines) override {
		out.cursor_up( lines );
	}
};



inline mpt::ustring append_software_tag( mpt::ustring software ) {
	mpt::ustring openmpt123 = mpt::ustring()
		+ MPT_USTRING("openmpt123 ")
		+ mpt::transcode<mpt::ustring>( mpt::source_encoding, OPENMPT123_VERSION_STRING )
		+ MPT_USTRING(" (libopenmpt ")
		+ mpt::transcode<mpt::ustring>( mpt::common_encoding::utf8, openmpt::string::get( "library_version" ) )
		+ MPT_USTRING(", OpenMPT ")
		+ mpt::transcode<mpt::ustring>( mpt::common_encoding::utf8, openmpt::string::get( "core_version" ) )
		+ MPT_USTRING(")")
		;
	if ( software.empty() ) {
		software = openmpt123;
	} else {
		software += MPT_USTRING(" (via ") + openmpt123 + MPT_USTRING(")");
	}
	return software;
}

inline mpt::ustring get_encoder_tag() {
	return mpt::ustring()
		+ MPT_USTRING("openmpt123 ")
		+ mpt::transcode<mpt::ustring>( mpt::source_encoding, OPENMPT123_VERSION_STRING )
		+ MPT_USTRING(" (libopenmpt ")
		+ mpt::transcode<mpt::ustring>( mpt::common_encoding::utf8, openmpt::string::get( "library_version" ) )
		+ MPT_USTRING(", OpenMPT ")
		+ mpt::transcode<mpt::ustring>( mpt::common_encoding::utf8, openmpt::string::get( "core_version" ) )
		+ MPT_USTRING(")");
}

inline mpt::native_path get_extension( mpt::native_path filename ) {
	mpt::native_path tmp = filename.GetFilenameExtension();
	if ( !tmp.empty() ) {
		tmp = mpt::native_path::FromNative( tmp.AsNative().substr( 1 ) );
	}
	return tmp;
}

enum class Mode {
	None,
	Probe,
	Info,
	UI,
	Batch,
	Render
};

inline mpt::ustring mode_to_string( Mode mode ) {
	switch ( mode ) {
		case Mode::None:   return MPT_USTRING("none"); break;
		case Mode::Probe:  return MPT_USTRING("probe"); break;
		case Mode::Info:   return MPT_USTRING("info"); break;
		case Mode::UI:     return MPT_USTRING("ui"); break;
		case Mode::Batch:  return MPT_USTRING("batch"); break;
		case Mode::Render: return MPT_USTRING("render"); break;
	}
	return MPT_USTRING("");
}

inline const std::int32_t default_low = -2;
inline const std::int32_t default_high = -1;

enum verbosity : std::int8_t {
	verbosity_shortversion = -1,
	verbosity_hidden = 0,
	verbosity_normal = 1,
	verbosity_verbose = 2,
};

struct commandlineflags {

	Mode mode = Mode::UI;
	std::int32_t ui_redraw_interval = default_high;
	mpt::ustring driver = MPT_USTRING("");
	mpt::ustring device = MPT_USTRING("");
	std::int32_t buffer = default_high;
	std::int32_t period = default_high;
	std::int32_t samplerate = MPT_OS_DJGPP ? 44100 : 48000;
	std::int32_t channels = 2;
	std::int32_t gain = 0;
	std::int32_t separation = 100;
	std::int32_t filtertaps = 8;
	std::int32_t ramping = -1; // ramping strength : -1:default 0:off 1 2 3 4 5 // roughly milliseconds
	std::int32_t tempo = 0;
	std::int32_t pitch = 0;
	std::int32_t dither = 1;
	std::int32_t repeatcount = 0;
	std::int32_t subsong = -1;
	std::map<std::string, std::string> ctls = {};
	double seek_target = 0.0;
	double end_time = 0.0;
	bool quiet = false;
	verbosity banner = verbosity_normal;
	bool verbose = false;
	bool assume_terminal = false;
	int terminal_width = -1;
	int terminal_height = -1;
	bool show_details = true;
	bool show_message = false;
	bool show_ui = true;
	bool show_progress = true;
	bool show_meters = true;
	bool show_channel_meters = false;
	bool show_pattern = false;
	bool use_float = MPT_OS_DJGPP ? false : mpt::float_traits<float>::is_hard && mpt::float_traits<float>::is_ieee754_binary;
	bool use_stdout = false;
	bool randomize = false;
	bool shuffle = false;
	bool restart = false;
	std::size_t playlist_index = 0;
	std::vector<mpt::native_path> filenames = {};
	mpt::native_path output_filename = MPT_NATIVE_PATH("");
	mpt::native_path output_extension = MPT_NATIVE_PATH("auto");
	bool force_overwrite = false;
	bool paused = false;
	mpt::ustring warnings = MPT_USTRING("");

	void apply_default_buffer_sizes() {
		if ( ui_redraw_interval == default_high ) {
			ui_redraw_interval = 50;
		} else if ( ui_redraw_interval == default_low ) {
			ui_redraw_interval = 10;
		}
		if ( buffer == default_high ) {
			buffer = 250;
		} else if ( buffer == default_low ) {
			buffer = 50;
		}
		if ( period == default_high ) {
			period = 50;
		} else if ( period == default_low ) {
			period = 10;
		}
	}

	void check_and_sanitize() {
		bool canUI = true;
		bool canProgress = true;
		if ( !assume_terminal ) {
#if MPT_OS_WINDOWS
			canUI = IsTerminal( 0 ) ? true : false;
			canProgress = IsTerminal( 2 ) ? true : false;
#else // !MPT_OS_WINDOWS
			canUI = isatty( STDIN_FILENO ) ? true : false;
			canProgress = isatty( STDERR_FILENO ) ? true : false;
#endif // MPT_OS_WINDOWS
		}
#if MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)
		HANDLE hStdOutput = GetStdHandle( STD_OUTPUT_HANDLE );
		if ( ( hStdOutput != NULL ) && ( hStdOutput != INVALID_HANDLE_VALUE ) ) {
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			ZeroMemory( &csbi, sizeof( CONSOLE_SCREEN_BUFFER_INFO ) );
			if ( GetConsoleScreenBufferInfo( hStdOutput, &csbi ) != FALSE ) {
				if ( terminal_width <= 0 ) {
					terminal_width = std::min( static_cast<int>( 1 + csbi.srWindow.Right - csbi.srWindow.Left ), static_cast<int>( csbi.dwSize.X ) );
				}
				if ( terminal_height <= 0 ) {
					terminal_height = std::min( static_cast<int>( 1 + csbi.srWindow.Bottom - csbi.srWindow.Top ), static_cast<int>( csbi.dwSize.Y ) );
				}
			}
		}
#else // !(MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10))
		if ( isatty( STDERR_FILENO ) ) {
			if ( terminal_width <= 0 ) {
				const char * env_columns = std::getenv( "COLUMNS" );
				if ( env_columns ) {
					int tmp = mpt::parse_or<int>( env_columns, 0 );
					if ( tmp > 0 ) {
						terminal_width = tmp;
					}
				}
			}
			if ( terminal_height <= 0 ) {
				const char * env_rows = std::getenv( "ROWS" );
				if ( env_rows ) {
					int tmp = mpt::parse_or<int>( env_rows, 0 );
					if ( tmp > 0 ) {
						terminal_height = tmp;
					}
				}
			}
			#if defined(TIOCGWINSZ)
				struct winsize ts;
				if ( ioctl( STDERR_FILENO, TIOCGWINSZ, &ts ) >= 0 ) {
					if ( terminal_width <= 0 ) {
						terminal_width = ts.ws_col;
					}
					if ( terminal_height <= 0 ) {
						terminal_height = ts.ws_row;
					}
				}
			#elif defined(TIOCGSIZE)
				struct ttysize ts;
				if ( ioctl( STDERR_FILENO, TIOCGSIZE, &ts ) >= 0 ) {
					if ( terminal_width <= 0 ) {
						terminal_width = ts.ts_cols;
					}
					if ( terminal_height <= 0 ) {
						terminal_height = ts.ts_rows;
					}
				}
			#endif
		}
#endif // MPT_OS_WINDOWS && !MPT_WINRT_BEFORE(MPT_WIN_10)
#if MPT_OS_DJGPP
		if ( terminal_width <= 0 ) {
			terminal_width = 80;
		}
		if ( terminal_height <= 0 ) {
			terminal_height = 25;
		}
#else
		if ( terminal_width <= 0 ) {
			terminal_width = 72;
		}
		if ( terminal_height <= 0 ) {
			terminal_height = 23;
		}
#endif
		if ( filenames.size() == 0 ) {
			throw args_nofiles_exception();
		}
		if ( use_stdout && ( device != commandlineflags().device || !output_filename.empty() ) ) {
			throw args_error_exception();
		}
		if ( !output_filename.empty() && ( device != commandlineflags().device || use_stdout ) ) {
			throw args_error_exception();
		}
		for ( const auto & filename : filenames ) {
			if ( filename == MPT_NATIVE_PATH("-") ) {
				canUI = false;
			}
		}
		show_ui = canUI;
		if ( !canProgress ) {
			show_progress = false;
		}
		if ( !canUI || !canProgress ) {
			show_meters = false;
		}
		if ( mode == Mode::None ) {
			if ( canUI ) {
				mode = Mode::UI;
			} else {
				mode = Mode::Batch;
			}
		}
		if ( mode == Mode::UI && !canUI ) {
			throw args_error_exception();
		}
		if ( show_progress && !canProgress ) {
			throw args_error_exception();
		}
		switch ( mode ) {
			case Mode::None:
				throw args_error_exception();
			break;
			case Mode::Probe:
				show_ui = false;
				show_progress = false;
				show_meters = false;
				show_channel_meters = false;
				show_pattern = false;
			break;
			case Mode::Info:
				show_ui = false;
				show_progress = false;
				show_meters = false;
				show_channel_meters = false;
				show_pattern = false;
			break;
			case Mode::UI:
			break;
			case Mode::Batch:
				show_meters = false;
				show_channel_meters = false;
				show_pattern = false;
			break;
			case Mode::Render:
				show_meters = false;
				show_channel_meters = false;
				show_pattern = false;
				show_ui = false;
			break;
		}
		if ( quiet ) {
			verbose = false;
			show_ui = false;
			show_details = false;
			show_progress = false;
			show_channel_meters = false;
		}
		if ( verbose ) {
			show_details = true;
		}
		if ( channels != 1 && channels != 2 && channels != 4 ) {
			channels = commandlineflags().channels;
		}
		if ( samplerate < 0 ) {
			samplerate = commandlineflags().samplerate;
		}
		if ( output_extension == MPT_NATIVE_PATH("auto") ) {
			output_extension = MPT_NATIVE_PATH("");
		}
		if ( mode != Mode::Render && !output_extension.empty() ) {
			throw args_error_exception();
		}
		if ( mode == Mode::Render && !output_filename.empty() ) {
			throw args_error_exception();
		}
		if ( mode != Mode::Render && !output_filename.empty() ) {
			output_extension = get_extension( output_filename );
		}
		if ( output_extension.empty() ) {
			output_extension = MPT_NATIVE_PATH("wav");
		}
	}

};

template < typename Tsample > Tsample convert_sample_to( float val );
template < > float convert_sample_to( float val ) {
	return val;
}
template < > std::int16_t convert_sample_to( float val ) {
	std::int32_t tmp = static_cast<std::int32_t>( val * 32768.0f );
	tmp = std::min( tmp, std::int32_t( 32767 ) );
	tmp = std::max( tmp, std::int32_t( -32768 ) );
	return static_cast<std::int16_t>( tmp );
}

class write_buffers_interface {
public:
	virtual ~write_buffers_interface() {
		return;
	}
	virtual void write_metadata( std::map<mpt::ustring, mpt::ustring> metadata ) {
		(void)metadata;
		return;
	}
	virtual void write_updated_metadata( std::map<mpt::ustring, mpt::ustring> metadata ) {
		(void)metadata;
		return;
	}
	virtual void write( const std::vector<float*> buffers, std::size_t frames ) = 0;
	virtual void write( const std::vector<std::int16_t*> buffers, std::size_t frames ) = 0;
	virtual bool pause() {
		return false;
	}
	virtual bool unpause() {
		return false;
	}
	virtual bool sleep( int /*ms*/ ) {
		return false;
	}
	virtual bool is_dummy() const {
		return false;
	}
};

class write_buffers_polling_wrapper : public write_buffers_interface {
protected:
	std::size_t channels;
	std::size_t sampleQueueMaxFrames;
	std::deque<float> sampleQueue;
protected:
	virtual ~write_buffers_polling_wrapper() {
		return;
	}
protected:
	write_buffers_polling_wrapper( const commandlineflags & flags )
		: channels(flags.channels)
		, sampleQueueMaxFrames(0)
	{
		return;
	}
	void set_queue_size_frames( std::size_t frames ) {
		sampleQueueMaxFrames = frames;
	}
	template < typename Tsample >
	Tsample pop_queue() {
		float val = 0.0f;
		if ( !sampleQueue.empty() ) {
			val = sampleQueue.front();
			sampleQueue.pop_front();
		}
		return convert_sample_to<Tsample>( val );
	}
public:
	void write( const std::vector<float*> buffers, std::size_t frames ) override {
		for ( std::size_t frame = 0; frame < frames; ++frame ) {
			for ( std::size_t channel = 0; channel < channels; ++channel ) {
				sampleQueue.push_back( buffers[channel][frame] );
			}
			while ( sampleQueue.size() >= sampleQueueMaxFrames * channels ) {
				while ( !forward_queue() ) {
					sleep( 1 );
				}
			}
		}
	}
	void write( const std::vector<std::int16_t*> buffers, std::size_t frames ) override {
		for ( std::size_t frame = 0; frame < frames; ++frame ) {
			for ( std::size_t channel = 0; channel < channels; ++channel ) {
				sampleQueue.push_back( buffers[channel][frame] * (1.0f/32768.0f) );
			}
			while ( sampleQueue.size() >= sampleQueueMaxFrames * channels ) {
				while ( !forward_queue() ) {
					sleep( 1 );
				}
			}
		}
	}
	virtual bool forward_queue() = 0;
	bool sleep( int ms ) override = 0;
};

class write_buffers_polling_wrapper_int : public write_buffers_interface {
protected:
	std::size_t channels;
	std::size_t sampleQueueMaxFrames;
	std::deque<std::int16_t> sampleQueue;
protected:
	virtual ~write_buffers_polling_wrapper_int() {
		return;
	}
protected:
	write_buffers_polling_wrapper_int( const commandlineflags & flags )
		: channels(flags.channels)
		, sampleQueueMaxFrames(0)
	{
		return;
	}
	void set_queue_size_frames( std::size_t frames ) {
		sampleQueueMaxFrames = frames;
	}
	std::int16_t pop_queue() {
		std::int16_t val = 0;
		if ( !sampleQueue.empty() ) {
			val = sampleQueue.front();
			sampleQueue.pop_front();
		}
		return val;
	}
public:
	void write( const std::vector<float*> buffers, std::size_t frames ) override {
		for ( std::size_t frame = 0; frame < frames; ++frame ) {
			for ( std::size_t channel = 0; channel < channels; ++channel ) {
				sampleQueue.push_back( convert_sample_to<std::int16_t>( buffers[channel][frame] ) );
			}
			while ( sampleQueue.size() >= sampleQueueMaxFrames * channels ) {
				while ( !forward_queue() ) {
					sleep( 1 );
				}
			}
		}
	}
	void write( const std::vector<std::int16_t*> buffers, std::size_t frames ) override {
		for ( std::size_t frame = 0; frame < frames; ++frame ) {
			for ( std::size_t channel = 0; channel < channels; ++channel ) {
				sampleQueue.push_back( buffers[channel][frame] );
			}
			while ( sampleQueue.size() >= sampleQueueMaxFrames * channels ) {
				while ( !forward_queue() ) {
					sleep( 1 );
				}
			}
		}
	}
	virtual bool forward_queue() = 0;
	bool sleep( int ms ) override = 0;
};

class void_audio_stream : public write_buffers_interface {
public:
	virtual ~void_audio_stream() {
		return;
	}
public:
	void write( const std::vector<float*> buffers, std::size_t frames ) override {
		(void)buffers;
		(void)frames;
	}
	void write( const std::vector<std::int16_t*> buffers, std::size_t frames ) override {
		(void)buffers;
		(void)frames;
	}
	bool is_dummy() const override {
		return true;
	}
};

class file_audio_stream_base : public write_buffers_interface {
protected:
	file_audio_stream_base() {
		return;
	}
public:
	void write_metadata( std::map<mpt::ustring, mpt::ustring> metadata ) override {
		(void)metadata;
		return;
	}
	void write_updated_metadata( std::map<mpt::ustring, mpt::ustring> metadata ) override {
		(void)metadata;
		return;
	}
	void write( const std::vector<float*> buffers, std::size_t frames ) override = 0;
	void write( const std::vector<std::int16_t*> buffers, std::size_t frames ) override = 0;
	virtual ~file_audio_stream_base() {
		return;
	}
};

} // namespace openmpt123

#endif // OPENMPT123_HPP
