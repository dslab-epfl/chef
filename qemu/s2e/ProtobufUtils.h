/*
 * ProtobufUtils.h
 *
 *  Created on: Mar 21, 2013
 *      Author: stefan
 */

#ifndef PROTOBUFUTILS_H_
#define PROTOBUFUTILS_H_

#include <ostream>
#include <string>

#include <stdint.h>
#include <google/protobuf/message.h>


namespace s2e {

struct framed_msg {
	typedef ::google::protobuf::Message message_type;

	const message_type &message;

	framed_msg(const message_type &_message) : message(_message) {}
};

inline std::ostream& operator<<(std::ostream& out, const framed_msg& fmsg)
{
	std::string messageString = fmsg.message.SerializeAsString();
	uint32_t messageSize = messageString.size();

	out.write((const char*) &messageSize, sizeof(messageSize));
	out.write(messageString.c_str(), messageSize);
	return out;
}

} // namespace s2e



#endif /* PROTOBUFUTILS_H_ */
