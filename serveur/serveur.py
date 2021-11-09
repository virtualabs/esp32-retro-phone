import select
from socket import *

class Phone:
  """
  This class acts as a data audio queue for a phone.
  """

  def __init__(self, socket):
    """
    Save socket and create a message queue
    """
    self.socket = socket
    self.queue = b''

  def recv(self, nb_bytes):
    return self.socket.recv(nb_bytes)

  def flush(self):
    if len(self.queue) > 0:
      ret = self.socket.send(self.queue)
      self.queue = b''
      return ret

  def enqueue(self, data):
    self.queue += data


class Dring:
  """
  VoIP-like server
  """

  def __init__(self, iface='0.0.0.0', port=8080):
    """
    Create server socket and bind it to given interface and port
    """
    self.serv_sock = socket(AF_INET, SOCK_STREAM)
    self.serv_sock.setsockopt(SOL_SOCKET, SO_REUSEADDR, 1)
    self.serv_sock.bind((iface, 8080))
    self.serv_sock.listen(5)

    # No phones connected yet
    self.phones = {}
    self.sockets = []

  def remove_phone(self, s):
    """
    Remove phone with socket s
    """
    self.sockets.remove(s)
    del(self.phones[s.fileno()])
    s.close()
    
  def run(self):
    """
    Start the VoIP server
    """
    while True:
      readable, writable, errored = select.select([self.serv_sock] + self.sockets, self.sockets, [self.serv_sock] + self.sockets)
      for s in readable:
        # A new connection ?
        if s == self.serv_sock:
          # Accept incoming connection
          client_sock, client_addr = self.serv_sock.accept()
          print('<- got connection from %s:%d' % client_addr)
          
          # Set client sock as non-blocking
          client_sock.setblocking(0)

          # Do we have a free slot ?
          if len(self.phones) < 2:
            self.sockets.append(client_sock)
            self.phones[client_sock.fileno()] = Phone(client_sock)
        else:
          # Get data from phone
          try:
            voip_data = s.recv(4096)
            if voip_data is not None:
              if len(self.sockets) == 1:
                try:
                  self.phones[s.fileno()].enqueue(voip_data)
                except BrokenPipeError as exc:
                  self.remove_phone(s)
              else:
                # Find the other phone and send data
                for phone in self.sockets:
                  if phone != s:
                    try:
                      self.phones[phone.fileno()].enqueue(voip_data)
                    except BrokenPipeError as exc:
                      self.remove_phone(phone)
            else:
              self.remove_phone(s)
          except ConnectionResetError as exc:
            self.remove_phone(s)


      for s in writable:
        if s.fileno() in self.phones:
          self.phones[s.fileno()].flush()



if __name__ == '__main__':
  server = Dring()
  server.run()

          




