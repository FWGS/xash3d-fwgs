#ifndef JSON_INCLUDED
#define JSON_INCLUDED

#ifdef _WIN32

typedef signed char			int8_t;
typedef short				int16_t;
typedef int					int32_t;
typedef __int64				int64_t;
typedef unsigned __int8		uint8_t;
typedef unsigned __int16	uint16_t;
typedef unsigned __int32	uint32_t;
typedef unsigned __int64	uint64_t;
typedef unsigned long		ulong;

#else

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#endif

#include <vector>
#include <string>
using namespace std;

class jvalue
{
	class flat_map;
public:
	enum jtype
	{
		E_NULL = 0,
		E_INT,
		E_BOOL,
		E_FLOAT,
		E_ARRAY,
		E_OBJECT,
		E_STRING,
		E_DATE,
		E_DATA,
	};

public:
	jvalue(jtype type = E_NULL);
	jvalue(int val);
	jvalue(bool val);
	jvalue(double val);
	jvalue(int64_t val);
	jvalue(const char* val);
	jvalue(const string& val);
	jvalue(const jvalue& other);
	jvalue(const char* val, size_t len);
	~jvalue();

	jvalue(jvalue&& other) noexcept;
	jvalue& operator=(jvalue&& other) noexcept;

public:
	int			as_int()	const;
	bool		as_bool()	const;
	double		as_double()	const;
	int64_t		as_int64()	const;
	string		as_string()	const;
	const char* as_cstr()	const;
	time_t		as_date()	const;
	string		as_data()	const;
	bool		as_data(string& data) const;

	void		assign_data(const char* base64);
	void		assign_data(const string& data);
	void		assign_data(const char* data, size_t size);
	void		assign_data(const uint8_t* data, size_t size);
	void		assign_date(time_t val);
	void		assign_date_string(time_t val);

	jtype		type() const;
	size_t		size() const;
	void		clear();

	const jvalue& at(int index) const;
	const jvalue& at(size_t index) const;
	const jvalue& at(const char* key) const;

	bool		has(int index) const;
	bool		has(size_t index) const;
	bool		has(const char* key) const;
	bool		has(const string& key) const;

	bool		erase(int index);
	bool		erase(size_t index);
	bool		erase(const char* key);
	bool		append(jvalue& jv);
	bool		get_keys(vector<string>& keys) const;
	int			index(const char* elem) const;

	jvalue& front();
	jvalue& back();

	bool		push_back(int val);
	bool		push_back(bool val);
	bool		push_back(double val);
	bool		push_back(int64_t val);
	bool		push_back(const char* val);
	bool		push_back(const string& val);
	bool		push_back(const jvalue& jval);
	bool		push_back(const char* val, size_t len);

	bool		is_int()	const;
	bool		is_null()	const;
	bool		is_bool()	const;
	bool		is_double()	const;
	bool		is_array()	const;
	bool		is_object()	const;
	bool		is_string()	const;
	bool		is_empty()	const;
	bool		is_data()	const;
	bool		is_date()	const;
	bool		is_data_string()	const;
	bool		is_date_string()	const;


	operator int()			const;
	operator bool()			const;
	operator double()		const;
	operator int64_t()		const;
	operator string()		const;
	operator const char* ()	const;

	jvalue& operator=(const jvalue& other);

	jvalue& operator[](int index);
	const jvalue& operator[](int index) const;

	jvalue& operator[](size_t index);
	const jvalue& operator[](size_t index) const;

	jvalue& operator[](int64_t index);
	const jvalue& operator[](int64_t index) const;

	jvalue& operator[](const char* key);
	const jvalue& operator[](const char* key) const;

	jvalue& operator[](const string& key);
	const jvalue& operator[](const string& key) const;

	friend bool	operator==(const jvalue& jv, const char* psz)
	{
		return (NULL != psz) ? (0 == strcmp(jv.as_cstr(), psz)) : jv.is_null();
	}

	friend bool	operator==(const char* psz, const jvalue& jv)
	{
		return (NULL != psz) ? (0 == strcmp(jv.as_cstr(), psz)) : jv.is_null();
	}

	friend bool	operator!=(const jvalue& jv, const char* psz)
	{
		return (NULL != psz) ? (0 != strcmp(jv.as_cstr(), psz)) : !jv.is_null();
	}

	friend bool	operator!=(const char* psz, const jvalue& jv)
	{
		return (NULL != psz) ? (0 != strcmp(jv.as_cstr(), psz)) : !jv.is_null();
	}

	friend bool	operator==(const jvalue& jv, const string& sz)
	{
		return (jv.as_cstr() == sz);
	}

	friend bool	operator==(const string& sz, const jvalue& jv)
	{
		return (jv.as_cstr() == sz);
	}

	friend bool	operator!=(const jvalue& jv, const string& sz)
	{
		return (jv.as_cstr() != sz);
	}

	friend bool	operator!=(const string& sz, const jvalue& jv)
	{
		return (jv.as_cstr() != sz);
	}

private:
	void	_free();
	void	_copy_value(const jvalue& src);
	char*	_new_string(const char* cstr);
	bool	_map_keys(vector<string>& keys) const;
	bool	_read_data_from_file(const char* path, string& data);
	bool	_write_data_to_file(const char* path, string& data);

public:
	static const jvalue	null;
	static const string null_data;

private:
	typedef vector<jvalue> array;
	typedef flat_map object;

	union _hold
	{
		bool					v_bool;
		double					v_double;
		int64_t					v_int64;
		char*					p_string;
		array*					p_array;
		object*					p_object;
		time_t					v_date;
		string*					p_data;
	} m_value;

	jtype m_type;

public:
	string			write() const;
	const char*		write(string& strdoc) const;

	string			write_to_html() const;


	string			style_write() const;
	const char*		style_write(string& strdoc) const;

	bool			read(const char* pdoc, string* pstrerr = NULL);
	bool			read(const string& strdoc, string* pstrerr = NULL);

	string			write_plist() const;
	const char*		write_plist(string& strdoc) const;

	string			style_write_plist() const;
	const char*		style_write_plist(string& strdoc) const;

	bool			write_bplist(string& strdoc) const;

	bool			read_plist(const string& strdoc, string* pstrerr = NULL, bool* is_binary = NULL);
	bool			read_plist(const char* pdoc, size_t len = 0, string* pstrerr = NULL, bool* is_binary = NULL);

	bool			read_from_file(const char* path, ...);
	bool			read_plist_from_file(const char* path, ...);

	bool			write_to_file(const char* path, ...);
	bool			write_plist_to_file(const char* path, ...);
	bool			write_bplist_to_file(const char* path, ...);
	bool			style_write_to_file(const char* path, ...);
	bool			style_write_plist_to_file(const char* path, ...);
};

//////////////////////////////////////////////////////////////////////////
// Custom ordered map: raw entry array (insertion order) + open-addressing hash
// - Small objects (< 32 keys): linear scan, zero hash overhead
// - Large objects: int32_t[] hash table with linear probing (~8 bytes/entry)
// - Preserves insertion order, minimal memory footprint

class jvalue::flat_map {
	static const uint32_t HASH_THRESHOLD = 32;
	static const int32_t EMPTY_SLOT = -1;

public:
	struct entry { string first; jvalue second; };
	typedef entry* iterator;
	typedef const entry* const_iterator;

private:
	entry*   m_entries;
	int32_t* m_buckets;
	uint32_t m_size;
	uint32_t m_cap;
	uint32_t m_bucket_count;

	static uint32_t _fnv1a(const char* s, size_t len) {
		uint32_t h = 2166136261u;
		for (size_t i = 0; i < len; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
		return h;
	}

	void _grow() {
		uint32_t nc = m_cap ? m_cap * 2 : 4;
		entry* ne = (entry*)::malloc(sizeof(entry) * nc);
		for (uint32_t i = 0; i < m_size; i++) {
			new(&ne[i].first) string(std::move(m_entries[i].first));
			new(&ne[i].second) jvalue(std::move(m_entries[i].second));
			m_entries[i].first.~string();
			m_entries[i].second.~jvalue();
		}
		::free(m_entries);
		m_entries = ne;
		m_cap = nc;
	}

	void _rehash() {
		m_bucket_count = 64;
		while (m_bucket_count < m_size * 2) m_bucket_count *= 2;
		::free(m_buckets);
		m_buckets = (int32_t*)::malloc(sizeof(int32_t) * m_bucket_count);
		::memset(m_buckets, 0xFF, sizeof(int32_t) * m_bucket_count);
		uint32_t mask = m_bucket_count - 1;
		for (uint32_t i = 0; i < m_size; i++) {
			uint32_t h = _fnv1a(m_entries[i].first.c_str(), m_entries[i].first.size()) & mask;
			while (m_buckets[h] != EMPTY_SLOT) h = (h + 1) & mask;
			m_buckets[h] = (int32_t)i;
		}
	}

	int32_t _probe(const string& key) const {
		if (!m_buckets) return -1;
		uint32_t mask = m_bucket_count - 1;
		uint32_t h = _fnv1a(key.c_str(), key.size()) & mask;
		while (m_buckets[h] != EMPTY_SLOT) {
			if (m_entries[m_buckets[h]].first == key) return m_buckets[h];
			h = (h + 1) & mask;
		}
		return -1;
	}

public:
	flat_map() : m_entries(NULL), m_buckets(NULL), m_size(0), m_cap(0), m_bucket_count(0) {}

	~flat_map() {
		for (uint32_t i = 0; i < m_size; i++) { m_entries[i].first.~string(); m_entries[i].second.~jvalue(); }
		::free(m_entries);
		::free(m_buckets);
	}

	flat_map(const flat_map& o) : m_entries(NULL), m_buckets(NULL), m_size(0), m_cap(0), m_bucket_count(0) {
		if (o.m_size) {
			m_cap = o.m_size;
			m_entries = (entry*)::malloc(sizeof(entry) * m_cap);
			for (uint32_t i = 0; i < o.m_size; i++) {
				new(&m_entries[i].first) string(o.m_entries[i].first);
				new(&m_entries[i].second) jvalue(o.m_entries[i].second);
			}
			m_size = o.m_size;
			if (o.m_buckets) _rehash();
		}
	}

	flat_map& operator=(const flat_map& o) {
		if (this != &o) {
			for (uint32_t i = 0; i < m_size; i++) { m_entries[i].first.~string(); m_entries[i].second.~jvalue(); }
			::free(m_entries); ::free(m_buckets);
			m_entries = NULL; m_buckets = NULL; m_size = 0; m_cap = 0; m_bucket_count = 0;
			if (o.m_size) {
				m_cap = o.m_size;
				m_entries = (entry*)::malloc(sizeof(entry) * m_cap);
				for (uint32_t i = 0; i < o.m_size; i++) {
					new(&m_entries[i].first) string(o.m_entries[i].first);
					new(&m_entries[i].second) jvalue(o.m_entries[i].second);
				}
				m_size = o.m_size;
				if (o.m_buckets) _rehash();
			}
		}
		return *this;
	}

	flat_map(flat_map&& o) noexcept
		: m_entries(o.m_entries), m_buckets(o.m_buckets),
		  m_size(o.m_size), m_cap(o.m_cap), m_bucket_count(o.m_bucket_count) {
		o.m_entries = NULL; o.m_buckets = NULL;
		o.m_size = 0; o.m_cap = 0; o.m_bucket_count = 0;
	}

	flat_map& operator=(flat_map&& o) noexcept {
		if (this != &o) {
			for (uint32_t i = 0; i < m_size; i++) { m_entries[i].first.~string(); m_entries[i].second.~jvalue(); }
			::free(m_entries); ::free(m_buckets);
			m_entries = o.m_entries; m_buckets = o.m_buckets;
			m_size = o.m_size; m_cap = o.m_cap; m_bucket_count = o.m_bucket_count;
			o.m_entries = NULL; o.m_buckets = NULL;
			o.m_size = 0; o.m_cap = 0; o.m_bucket_count = 0;
		}
		return *this;
	}

	jvalue& operator[](const string& key) {
		if (m_buckets) {
			int32_t idx = _probe(key);
			if (idx >= 0) return m_entries[idx].second;
		} else {
			for (uint32_t i = 0; i < m_size; i++) {
				if (m_entries[i].first == key) return m_entries[i].second;
			}
		}
		if (m_size == m_cap) _grow();
		new(&m_entries[m_size].first) string(key);
		new(&m_entries[m_size].second) jvalue();
		m_size++;
		if (!m_buckets && m_size >= HASH_THRESHOLD) {
			_rehash();
		} else if (m_buckets) {
			if (m_size * 2 > m_bucket_count) { _rehash(); }
			else {
				uint32_t mask = m_bucket_count - 1;
				uint32_t h = _fnv1a(key.c_str(), key.size()) & mask;
				while (m_buckets[h] != EMPTY_SLOT) h = (h + 1) & mask;
				m_buckets[h] = (int32_t)(m_size - 1);
			}
		}
		return m_entries[m_size - 1].second;
	}

	const_iterator find(const string& key) const {
		if (m_buckets) {
			int32_t idx = _probe(key);
			return (idx >= 0) ? m_entries + idx : m_entries + m_size;
		}
		for (uint32_t i = 0; i < m_size; i++) {
			if (m_entries[i].first == key) return m_entries + i;
		}
		return m_entries + m_size;
	}

	iterator find(const string& key) {
		if (m_buckets) {
			int32_t idx = _probe(key);
			return (idx >= 0) ? m_entries + idx : m_entries + m_size;
		}
		for (uint32_t i = 0; i < m_size; i++) {
			if (m_entries[i].first == key) return m_entries + i;
		}
		return m_entries + m_size;
	}

	void erase(const string& key) {
		int32_t idx = -1;
		if (m_buckets) { idx = _probe(key); }
		else { for (uint32_t i = 0; i < m_size; i++) { if (m_entries[i].first == key) { idx = (int32_t)i; break; } } }
		if (idx < 0) return;
		m_entries[idx].first.~string();
		m_entries[idx].second.~jvalue();
		for (uint32_t i = (uint32_t)idx; i + 1 < m_size; i++) {
			new(&m_entries[i].first) string(std::move(m_entries[i+1].first));
			new(&m_entries[i].second) jvalue(std::move(m_entries[i+1].second));
			m_entries[i+1].first.~string();
			m_entries[i+1].second.~jvalue();
		}
		m_size--;
		if (m_buckets) {
			if (m_size < HASH_THRESHOLD / 2) { ::free(m_buckets); m_buckets = NULL; m_bucket_count = 0; }
			else _rehash();
		}
	}

	size_t size() const { return m_size; }
	bool empty() const { return 0 == m_size; }
	iterator begin() { return m_entries; }
	iterator end() { return m_entries + m_size; }
	const_iterator begin() const { return m_entries; }
	const_iterator end() const { return m_entries + m_size; }
};

//////////////////////////////////////////////////////////////////////////

class jreader
{
public:
	jreader();

	static const int MAX_DEPTH = 256;

public:
	bool	parse(const char* pdoc, jvalue& root);
	void	error(string& strmsg) const;

private:
	struct jtoken
	{
		enum jtoken_type
		{
			E_JTOKEN_ERROR = 0,
			E_JTOKEN_END,
			E_JTOKEN_NULL,
			E_JTOKEN_TRUE,
			E_JTOKEN_FALSE,
			E_JTOKEN_NUMBER,
			E_JTOKEN_STRING,
			E_JTOKEN_ARRAY_BEGIN,
			E_JTOKEN_ARRAY_END,
			E_JTOKEN_OBJECT_BEGIN,
			E_JTOKEN_OBJECT_END,
			E_JTOKEN_ARRAY_SEPARATOR,
			E_JTOKEN_MEMBER_SEPARATOR
		};

		jtoken_type type;
		const char* pbegin;
		const char* pend;
	};

	void	_skip_spaces();
	void	_skip_comment();

	bool	_match(const char* pattern, int pattern_length);

	bool	_read_token(jtoken& token);
	bool	_read_value(jvalue& jval);
	bool	_read_array(jvalue& jval);
	void	_read_number();

	bool	_read_string();
	bool	_read_object(jvalue& jval);

	bool	_decode_number(jtoken& token, jvalue& jval);
	bool	_decode_string(jtoken& token, string& decoded);
	bool	_decode_double(jtoken& token, jvalue& jval);

	char	_get_next_char();
	bool	_add_error(const string& message, const char* ploc);

private:
	const char* m_pbegin;
	const char* m_pend;
	const char* m_pcursor;
	const char* m_perror;
	string		m_strerr;
	int			m_depth;
};

class jwriter
{
public:
	jwriter();

public:
	static	void	write(const jvalue& jval, string& strdoc);
	static	void	write_to_html(const jvalue& jval, string& strdoc);

private:
	static	void	_write_value(const jvalue& jval, string& strdoc);
	static	void	_write_value_to_html(const jvalue& jval, string& strdoc);

public:
	const string& style_write(const jvalue& jval);

private:
	void	_push_value(const string& strval);
	void	_style_write_value(const jvalue& jval);
	void	_style_write_array_value(const jvalue& jval);
	bool	_is_multiline_array(const jvalue& jval);

public:
	static  string	d2s(time_t t);

private:
	static	string	v2s(double val);
	static	string	v2s(int64_t val);
	static	string	v2s(const char* val);
	static	string	vstring2s(const char* val);
	static  string	vstring2html(const char* pstr);

private:
	string			m_tab;
	string			m_indent;
	string			m_strdoc;

private:
	bool			m_add_child;
	vector<string>	m_child_values;
};


//////////////////////////////////////////////////////////////////////////

class jpreader
{
public:
	jpreader();

public:
	enum bplist_object_type
	{
		BPLIST_NULL = 0x00,
		NS_NUMBER_FALSE = 0x08,
		NS_NUMBER_TRUE = 0x09,
		NS_URL_BASE_STRING = 0x0C,
		NS_URL_STRING = 0x0D,
		NS_UUID = 0x0E,
		BPLIST_FILL = 0x0F,
		NS_NUMBER_INT = 0x10,
		NS_NUMBER_REAL = 0x20,
		NS_DATE = 0x30,
		NS_DATA = 0x40,
		NS_STRING_ASCII = 0x50,
		NS_STRING_UNICODE = 0x60,
		NS_STRING_UTF8 = 0x70,
		BPLIST_UID = 0x80,
		NS_ARRAY = 0xA0,
		NS_ORDEREDSET = 0xB0,
		NS_SET = 0xC0,
		NS_DICTIONARY = 0xD0
	};

public:
	bool	parse(const char* pdoc, size_t len, jvalue& root, bool* is_binary);
	void	error(string& strmsg) const;

private:
	struct ptoken
	{
		enum ptoken_type
		{
			E_PTOKEN_ERROR = 0,
			E_PTOKEN_END,
			E_PTOKEN_NULL,
			E_PTOKEN_TRUE,
			E_PTOKEN_FALSE,
			E_PTOKEN_KEY,
			E_PTOKEN_DATA,
			E_PTOKEN_DATA_NULL,
			E_PTOKEN_DATE,
			E_PTOKEN_DATE_NULL,
			E_PTOKEN_NUMBER,
			E_PTOKEN_INTEGER_NULL,
			E_PTOKEN_REAL_NULL,
			E_PTOKEN_STRING,
			E_PTOKEN_STRING_NULL,
			E_PTOKEN_ARRAY_BEGIN,
			E_PTOKEN_ARRAY_END,
			E_PTOKEN_ARRAY_NULL,
			E_PTOKEN_DICTIONARY_BEGIN,
			E_PTOKEN_DICTIONARY_END,
			E_PTOKEN_DICTIONARY_NULL
		};

		ptoken()
		{
			pbegin = NULL;
			pend = NULL;
			type = E_PTOKEN_ERROR;
		}

		ptoken_type type;
		const char* pbegin;
		const char* pend;

	};

	bool	_read_token(ptoken& token);
	bool	_read_label(string& label);
	bool	_read_value(jvalue& jval, ptoken& token);
	bool	_read_array(jvalue& jval);
	bool	_read_number();

	bool	_read_string();
	bool	_read_dictionary(jvalue& jval);

	void	_end_label(ptoken& token, const char* end_label);

	bool	_decode_number(ptoken& token, jvalue& jval);
	bool	_decode_string(ptoken& token, string& decoded);
	bool	_decode_double(ptoken& token, jvalue& jval);

	void	_skip_spaces();
	bool	_add_error(const string& message, const char* ploc);


public:
	bool	parse_binary(const char* pbdoc, size_t len, jvalue& pv);

	static const int MAX_DEPTH = 256;

private:
	uint32_t	_get_uint24_from_be(const char* v);
	bool		_get_uint_val_safe(const char* v, size_t size, uint64_t& out);
	bool		_read_uint_size(const char*& pcur, size_t& size);
	bool		_read_binary_value(const char*& pcur, jvalue& pv);
	bool		_read_unicode(const char* pcur, size_t size, jvalue& pv);
	bool		_bp_in_bounds(const char* p, size_t n) const;

private: //xml
	const char* m_pbegin;
	const char* m_pend;
	const char* m_pcursor;
	const char* m_perror;
	string		m_strerr;
	int			m_depth;

private: //binary
	const char* m_ptrailer;
	uint64_t	m_num_objects;
	uint64_t	m_top_object_offset;
	const char* m_poffset_table;
	uint8_t		m_object_ref_size;
	uint8_t		m_offset_table_offset_size;
};

class jpwriter
{
public:
	jpwriter();

public:
	void			write(const jvalue& pval, string& strdoc);
	void			write_to_binary(const jvalue& pval, string& strdoc);
	const string&	style_write(const jvalue& pval);

private:
	struct bplist_object
	{
		bplist_object()
		{
			type = jpreader::BPLIST_NULL;
		}
		string data;
		vector<uint64_t> indexes;
		jpreader::bplist_object_type type;
	};

private:
	static	uint8_t	_get_integer_bytes(int64_t value);
	static	uint8_t	_get_integer_length(int64_t value);
	static	int		_get_serialize_integer(uint8_t* data, uint8_t length, int64_t value);
	static	void	_serialize_object_size(string& data, jpreader::bplist_object_type object_type, int64_t object_size);

	static  bool	_contains_unicode_char(const char* str, size_t len);
	static  bool	_utf8_to_utf16(const char* utf8, size_t len, string& utf16);
	static	int64_t	_write_value_to_binary(const jvalue& pval, vector<bplist_object>& array_objects, vector<uint64_t>& array_offset_indexes);

private:
	void _style_write_value(const jvalue& pval);
	const string& _style_write(const jvalue& pval);

public:
	static inline uint16_t	_swap(uint16_t value);
	static inline uint32_t	_swap(uint32_t value);
	static inline uint64_t	_swap(uint64_t value);
	static inline float		_swap(float value);
	static inline double	_swap(double value);
	static inline void		_byte_convert(uint8_t* v, size_t size);

private:
	static void		_xml_escape(string& str);
	static string&	_string_replace(string& context, const string& from, const string& to);

private:
	string			m_tab;
	string			m_line;
	string			m_indent;
	string			m_strdoc;
};

//////////////////////////////////////////////////////////////////////////

class jbase64
{
public:
	jbase64();
	~jbase64();

public:
	const char* encode(const char* src, int src_len = 0);
	const char* encode(const string& input);
	const char* decode(const char* src, int src_len = 0, int* pdecode_len = NULL);
	const char* decode(const char* src, string& output);

private:
	inline int get_b64_index(char ch);
	inline char get_b64_char(int index);

private:
	vector<char*> m_array_decodes;
	vector<char*> m_array_encodes;
};

#endif // JSON_INCLUDED
