/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright 2012 Google Inc. All Rights Reserved.
 * Author: sbucur@google.com (Stefan Bucur)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
 */


#ifndef KLEE_DATA_SUPPORT_H_
#define KLEE_DATA_SUPPORT_H_

#include <istream>
#include <ostream>
#include <string>
#include <stdint.h>

#include <google/protobuf/message.h>

#include <llvm/Support/raw_ostream.h>

namespace google {
namespace protobuf {
class Message;
}
}


namespace klee {


template<class Stream>
bool ReadNextMessage(Stream &is, std::string &message) {
  message.clear();

  uint32_t message_size;
  is.read((char*)&message_size, sizeof(message_size));
  if (sizeof(message_size) != is.gcount()) {
    return false;
  }

  if (!message_size) {
      return true;
  }

  char *message_buffer = new char[message_size];
  is.read(message_buffer, message_size);
  if (message_size != is.gcount()) {
    delete[] message_buffer;
    return false;
  }

  message.assign(message_buffer, message_size);
  delete[] message_buffer;
  return true;
}


template<class Stream>
void WriteProtoMessage(const ::google::protobuf::Message &message,
    Stream &os, bool framed) {

  std::string messageString = message.SerializeAsString();
  if (framed) {
	uint32_t messageSize = messageString.size();

	os.write((const char*)&messageSize, sizeof(messageSize));
	os.write(messageString.c_str(), messageSize);
  } else {
	os << messageString;
  }
  os.flush();

}


}

#endif  // INCLUDE_KLEE_DATA_SUPPORT_H_
