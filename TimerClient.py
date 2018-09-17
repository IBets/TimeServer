import socket

UDP_IP   = "127.0.0.1"
UDP_PORT = 123
MESSAGE  = b"Get time"

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    while(True):
        sock.sendto(MESSAGE, (UDP_IP, UDP_PORT))
        conn, addr = sock.recvfrom(1024)
        print(addr, "->", str(conn))
      

if __name__ == "__main__":
    main()