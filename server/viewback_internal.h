#pragma once

#pragma warning(disable: 4201) /* nonstandard extension used : nameless struct/union */

static_assert(sizeof(unsigned long long int) == 8, "unsigned long long int must be 64 bits.");

#ifdef VIEWBACK_TIME_DOUBLE
typedef double vb_time_t;
#else
typedef vb_uint64 vb_time_t;
#endif


// ============= Viewback stuff =============

#define CHANNEL_FLAG_INITIALIZED (1<<0)
// If you add more than 8, bump the size of vb_data_channel_t::flags

// This isn't really always 1 byte long. It's a bit mask large enough to hold
// all channels, so it may be longer.
typedef unsigned char vb_data_channel_mask_t;
#define VB_CHANNEL_NONE ((vb_channel_handle_t)~0)
#define VB_GROUP_NONE ((vb_group_handle_t)~0)

typedef struct
{
	const char*    name;
	vb_data_type_t type;
	float          range_min;
	float          range_max;

	unsigned char  flags; // CHANNEL_FLAG_*

	// If this is nonzero it means that we threw out some redundant data. Next
	// time we send data to the client we should let it know we threw some out.
	vb_time_t      maintain_time;

	union
	{
		int   last_int;
		float last_float;
		struct
		{
			float last_float_x;
			float last_float_y;
			float last_float_z;
		};
	};
} vb_data_channel_t;

typedef struct
{
	const char* name;
} vb_data_group_t;

typedef struct
{
	vb_group_handle_t   group;
	vb_channel_handle_t channel;
} vb_data_group_member_t;

typedef struct
{
	vb_channel_handle_t handle;
	int                 value;
	const char*         name;
} vb_data_label_t;

typedef struct
{
	vb_socket_t socket;

	vb_data_channel_mask_t* active_channels;
} vb_connection_t;

typedef struct
{
	vb_config_t config;

	vb_socket_t        multicast_socket;
	struct sockaddr_in multicast_addr;
	time_t             last_multicast;
	vb_socket_t        tcp_socket;

	vb_data_channel_t* channels;
	size_t             next_channel;

	vb_data_group_t* groups;
	size_t           next_group;

	vb_data_group_member_t* group_members;
	size_t                  next_group_member;

	vb_data_label_t* labels;
	size_t           next_label;

	vb_connection_t* connections;
	bool             server_active;

	vb_time_t        current_time;
} vb_t;





// ============= Protobuf stuff =============

// Haha don't do this in a function or it will be quickly freed, it's alloca. :)
#ifdef _DEBUG
#define Packet_alloca(length) alloca(length + 1024)
#else
// Add sizeof(size_t) bytes because we're going to prepend the length of the message.
#define Packet_alloca(length) alloca(length + sizeof(size_t))
#endif

struct Data {
	unsigned long  _handle;
	vb_data_type_t _type; // Won't get sent over the wire, it's needed to tell which data to send.
	unsigned long  _data_int;
	float          _data_float;
	float          _data_float_x;
	float          _data_float_y;
	float          _data_float_z;

#ifdef VIEWBACK_TIME_DOUBLE
	double                 _time_double;
	double                 _maintain_time_double;
#else
	unsigned long long int _time_uint64;
	unsigned long long int _maintain_time_uint64;
#endif
};

struct DataChannel {
	int            _field_name_len;
	const char*    _field_name;
	vb_data_type_t _type;
	unsigned long  _handle;
	float          _min;
	float          _max;
};

struct DataGroup {
	const char*    _name;
	int            _name_len;
	unsigned long* _channels;
	int            _channels_repeated_len;
};

struct DataLabel {
	unsigned long  _handle;
	int            _value;
	int            _field_name_len;
	const char*    _field_name;
};

struct Packet {
	struct Data*        _data;
	int                 _data_channels_repeated_len;
	struct DataChannel* _data_channels;
	int                 _data_groups_repeated_len;
	struct DataGroup*   _data_groups;
	int                 _data_labels_repeated_len;
	struct DataLabel*   _data_labels;

	int            _console_output_len;
	const char*    _console_output;

	int            _status_len;
	const char*    _status;
};

void Packet_initialize(struct Packet* packet);
void Packet_initialize_data(struct Packet* packet, struct Data* data, vb_data_type_t type);
void Packet_initialize_registrations(struct Packet* packet, struct DataChannel* data_channels, size_t channels, struct DataGroup* data_groups, size_t groups, struct DataLabel* data_labels, size_t labels);
size_t Packet_get_message_size(struct Packet *_Packet);
size_t Packet_serialize(struct Packet *_Packet, void *_buffer, size_t length);




