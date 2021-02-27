import socket
import sys

port = 37020

con = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)

con.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
con.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
con.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

try:
    con.bind(('<broadcast>', port))
except socket.error:
    print('Connection failed')
    con.close()
    sys.exit()

print(f"Listening to broadcasts on port {port}:")

while 1:
    data, _ = con.recvfrom(1024)
    a = data.decode('utf-8')
    print(a)