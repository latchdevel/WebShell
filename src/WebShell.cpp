#include "WebShell.h"         // https://github.com/latchdevel/WebShell

#include <Shell.h>            // https://github.com/latchdevel/Shell
#include <WebSocketServer.h>  // https://github.com/latchdevel/mWebSockets

// --------------------------------------- WebSocket Stream -------------------------------------------- //

// Constructor implementation (member initialization list handles everything)
WebSocketStream::WebSocketStream() : m_ws(nullptr), m_head(0), m_tail(0) {}

void WebSocketStream::attachSocket(net::WebSocket& ws) {
   m_ws = &ws;
   m_head = 0;
   m_tail = 0;
}

void WebSocketStream::detachSocket() {
   m_ws = nullptr;
   m_head = 0;
   m_tail = 0;
}

void WebSocketStream::pushData(const uint8_t* data, uint8_t length) {
   // Cache volatile values locally for this execution scope
   uint8_t currentHead = m_head;
   uint8_t currentTail = m_tail;

   for (uint8_t i = 0; i < length; i++) {
      uint8_t nextHead = (currentHead + 1) % WS_RX_BUFFER_SIZE;
      
      // Check if the ring buffer is full
      if (nextHead != currentTail) {
         m_rxBuffer[currentHead] = data[i];
         currentHead = nextHead;
      } else {
         // Buffer overflow condition: discard the remaining incoming bytes
         break; 
      }
   }
   // Update the volatile head index to notify the reading thread
   m_head = currentHead;
}

int WebSocketStream::available() {
   // Perform calculations using native 8-bit types to save CPU cycles on AVR,
   // then implicit promotion will convert the final result to int (16-bit).
   uint8_t headCopy = m_head;
   uint8_t tailCopy = m_tail;
   
   return (WS_RX_BUFFER_SIZE + headCopy - tailCopy) % WS_RX_BUFFER_SIZE;
}

int WebSocketStream::read() {
   uint8_t headCopy = m_head;
   uint8_t tailCopy = m_tail;

   if (headCopy == tailCopy) {
      return -1; // Buffer is empty
   }

   uint8_t value = m_rxBuffer[tailCopy];
   m_tail = (tailCopy + 1) % WS_RX_BUFFER_SIZE;
   
   return value;
}

int WebSocketStream::peek() {
   if (m_head == m_tail) {
      return -1; // Buffer is empty
   }
   return m_rxBuffer[m_tail];
}

size_t WebSocketStream::write(uint8_t byte) {
   // Verify that the socket pointer is valid and the connection is active
   if (m_ws && m_ws->isAlive()) {
      m_ws->send(net::WebSocket::DataType::BINARY, reinterpret_cast<const char*>(&byte), 1);
      return 1;
   }
   return 0;
}

size_t WebSocketStream::write(const uint8_t* buffer, size_t size) {
   // Bulk transmission bypasses single byte overhead, maximizing 8-bit performance
   if (m_ws && m_ws->isAlive()) {
      m_ws->send(net::WebSocket::DataType::BINARY, reinterpret_cast<const char*>(buffer), size);
      return size;
   }
   return 0;
}

void WebSocketStream::flush() {
   // Intentionally left blank. Outgoing data is pushed instantly to the network stack.
}

// --------------------------------------- WebShell Manager -------------------------------------------- //

WebShellManager::WebShellManager(uint8_t maxWebShellSessions) : maxWebShellSessions(maxWebShellSessions) {
   // Initialize the fixed session pool allocation dynamically
   m_sessions = new Session[maxWebShellSessions];

   // Explicitly initialize the session slots to empty states safely for AVR 8-bit
   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      m_sessions[i].socket = nullptr;
      m_sessions[i].stream = nullptr;
      m_sessions[i].shell = nullptr;
      m_sessions[i].startTime = 0;
   }
}

WebShellManager::~WebShellManager() {
   // Iterate through all slots in the fixed pool to terminate any lingering sessions
   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      releaseSessionSlot(i);
   }
   delete[] m_sessions;
}

void WebShellManager::releaseSessionSlot(uint8_t index) {
   // Bounds safety check to prevent out-of-bounds memory access on the AVR array
   if (index >= maxWebShellSessions) {
      return; 
   }

   // Safely detach and deallocate the WebSocket Stream Adapter
   if (m_sessions[index].stream != nullptr) {
      m_sessions[index].stream->detachSocket(); // Updated to use the technical 'detachSocket' name
      delete m_sessions[index].stream;
      m_sessions[index].stream = nullptr;
   }
   
   // Safely deallocate the Shell instance
   if (m_sessions[index].shell != nullptr) {
      delete m_sessions[index].shell;
      m_sessions[index].shell = nullptr;
   }
   
   // Unbind the native pointer. The original WebSocket instance is managed externally and not deleted here.
   m_sessions[index].socket = nullptr;
   m_sessions[index].startTime = 0;
}

void WebShellManager::onConnect(net::WebSocket& ws, size_t maxHistory) {
   // Iterate through the fixed pool to find an available (null) session slot
   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      if (m_sessions[i].socket == nullptr) {

         // Bind network socket and allocate lifecycle dependencies on the heap
         m_sessions[i].socket    = &ws;
         m_sessions[i].stream    = new WebSocketStream();
         m_sessions[i].shell     = new Shell(); 
         m_sessions[i].startTime = millis();
         
         // Attach the active network socket to our newly created stream adapter
         m_sessions[i].stream->attachSocket(ws);

         // Initialize the Shell instance, passing the stream adapter as a dereferenced object
         m_sessions[i].shell->begin(*(m_sessions[i].stream), maxHistory, Terminal::Mode::Telnet);

         return; 
      }
   }
   // If execution reaches this point, the session pool is full. 
   // It is only possible if WebSocket maxConnections > WebShellManager maxWebShellSessions
   // Reject the connection here by calling ws.close()
   const char* POOL_FULL = "ERROR: WebShell session pool is full!\r\n";
   if (ws.isAlive()) ws.send(net::WebSocket::DataType::BINARY, POOL_FULL, strlen(POOL_FULL));
   ws.close(net::WebSocket::CloseCode::TRY_AGAIN_LATER, true);
}

void WebShellManager::onDisconnect(net::WebSocket& ws) {
   // Scan the session pool to find the slot matching the disconnected socket pointer
   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      if (m_sessions[i].socket == &ws) {
         // Safely deallocate resources and clear the slot tracking metadata
         releaseSessionSlot(i);
         return;
      }
   }
}

void WebShellManager::onData(net::WebSocket& ws, const char* data, size_t length) {
   // Scan the active pool to find the session corresponding to the sending socket
   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      if (m_sessions[i].socket == &ws && m_sessions[i].stream != nullptr) {
         // Safely interpret the text payload buffer as unsigned raw bytes for the 8-bit stream adapter.
         // Cast length to uint8_t to strictly match the AVR-optimized pushData signature.
         m_sessions[i].stream->pushData(
            reinterpret_cast<const uint8_t*>(data), 
            static_cast<uint8_t>(length)
         );
         return;
      }
   }
}

void WebShellManager::poll() {
   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      // Skip empty or inactive slots
      if (m_sessions[i].socket != nullptr && m_sessions[i].shell != nullptr) {
         m_sessions[i].shell->loop();
      }
   }
}

void WebShellManager::closeAll() {
   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      if (m_sessions[i].socket != nullptr) {
         close(m_sessions[i].socket);
      }
   }
}

void WebShellManager::close(net::WebSocket* ws) {
   if (ws) ws->close(net::WebSocket::CloseCode::NORMAL_CLOSURE, true);
}

net::WebSocket* WebShellManager::getSessionSocket(uint8_t index) const {
   // Safety check: ensure the index is within valid bounds
   if (index >= maxWebShellSessions) return nullptr;
    
   // Return the socket pointer if the session is currently active
   if (m_sessions[index].socket != nullptr) {
      return m_sessions[index].socket;
   }
    
   // Default fallback if no valid socket exists
   return nullptr;
}

uint8_t WebShellManager::getActiveSessionsCount() const {
   uint8_t count = 0;
   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      // Count the slot if it has an assigned socket (it is active)
      if (m_sessions[i].socket != nullptr) {
         count++;
      }
   }
   return count;
}

net::WebSocket* WebShellManager::getSocketByShell(const Shell& shell) const {
   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      // Check if the slot contains the matching shell reference
      if (m_sessions[i].shell == &shell) {
         return m_sessions[i].socket;
      }
   }
   return nullptr; // No match found
}

Shell* WebShellManager::getShellBySocket(const net::WebSocket* socket) const {
   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      // Check if the slot contains the matching socket pointer
      if (m_sessions[i].socket == socket) {
         return m_sessions[i].shell;
      }
   }
   return nullptr; // No match found
}

uint32_t WebShellManager::getSessionAgeInSeconds(net::WebSocket* ws) const {
   if (!ws) return 0;

   for (uint8_t i = 0; i < maxWebShellSessions; i++) {
      if (m_sessions[i].socket == ws) {
         // Safe arithmetic subtraction to prevent millis() overflow every ~49 days
         uint32_t elapsedMillis = millis() - m_sessions[i].startTime;
         return elapsedMillis / 1000; // Convert milliseconds to seconds
      }
   } 
   return 0; // Session not found
}
