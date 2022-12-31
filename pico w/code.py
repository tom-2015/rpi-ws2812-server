from ipaddress import IPv4Address
import time
import os
import wifi
import socketpool
import microcontroller
import struct
import neopixel
import board

#by default al settings are loaded from settings.toml except the pin number

#define the pin where LED string is attached
pixel_pin =  board.GP0 # os.getenv("PIN") how to load pin from settings??

#define wifi SSID password, dhcp and IP
WIFI_SSID = os.getenv('WIFI_SSID')
WIFI_PASSWORD = os.getenv('WIFI_PASSWORD')
use_dhcp = os.getenv("USE_DHCP")
ip = IPv4Address(os.getenv("IP"))
netmask = IPv4Address(os.getenv("NETMASK"))
gateway = IPv4Address(os.getenv("GATEWAY"))
port = os.getenv("PORT")

#define number of LEDs in your string here
pixel_count = os.getenv("PIXEL_COUNT")

#define here number of colors per pixel, default is 3 (RGB), use 4 if you use RGBW pixels
pixel_color_size = os.getenv("PIXEL_COLOR_COUNT")

#define here the pixel color order, RGB, RGBW, ...
pixel_order = os.getenv("PIXEL_COLOR_ORDER") #neopixel.RGB

#https://circuitpython-jake.readthedocs.io/en/latest/shared-bindings/socket/__init__.html
#https://docs.circuitpython.org/projects/neopixel/en/latest/

#typedef struct {
#	unsigned char signature; 0
#	unsigned char version; 1
#	unsigned short reserved; 2
#	unsigned int command; 3
#	unsigned int flags; 4
#	unsigned int packet_nr; 5
#	unsigned int data_size; 6
#} slave_channel_packet;

SLAVE_PACKET_SIZE = 20
SLAVE_SIGNATURE = 0xFB
SLAVE_PROTOCOL_VERSION = 0x1
SLAVE_COMMAND_RENDER = 0x1
SLAVE_COMMAND_CMD_OK = 0x2

pixels = neopixel.NeoPixel(pixel_pin, pixel_count, pixel_order=pixel_order, auto_write=False)
pixels.show()

print("INIT OK")
wifi.radio.connect(WIFI_SSID, WIFI_PASSWORD)
if use_dhcp:
    print("Finding IP address")
    print(wifi.radio.ipv4_address)
else:
    print("Using Fixed IP ", ip)
    wifi.radio.set_ipv4_address(ipv4 = ip, netmask = netmask, gateway= gateway)

pool = socketpool.SocketPool(wifi.radio)

print("Creating socket")
sock = pool.socket(pool.AF_INET, pool.SOCK_STREAM)

host = str(wifi.radio.ipv4_address)
sock.bind((host, port))
sock.listen(1)
print("Accepting connections at ", host, ":", port)


def send_slave_packet(conn: socketpool.Socket, command: int, flags: int, packet_nr: int):
    packet = struct.pack("BBHIIII", SLAVE_SIGNATURE, SLAVE_PROTOCOL_VERSION, 0, command, flags, packet_nr, 0)
    conn.send(packet)

def receive_slave_packet(conn, buff):
    #https://docs.python.org/3/library/struct.html
    packet = struct.unpack("BBHIIII", bytes(buff))
    if (packet[0]== SLAVE_SIGNATURE and packet[3]==SLAVE_COMMAND_RENDER):
        data_size = packet[6]
        led_data = bytearray(data_size)
        conn.recv_into(led_data, data_size)
        for i in range(pixel_count):
            pixels[i] = struct.unpack_from("BBB", bytes(led_data), i * pixel_color_size)
        pixels.show()
        send_slave_packet(conn, SLAVE_COMMAND_CMD_OK, 0, packet[5])

while True:
    try:
        conn, addr = sock.accept()
        print("Connected by ", addr)
        buff = bytearray(SLAVE_PACKET_SIZE)
        conn.setblocking(True)
        numbytes = conn.recv_into(buff, SLAVE_PACKET_SIZE)
        while numbytes > 0:
            if numbytes == SLAVE_PACKET_SIZE:
                receive_slave_packet(conn, buff)
            numbytes = conn.recv_into(buff, SLAVE_PACKET_SIZE)
    except OSError as e:
        print("Error: ", e)
    except Exception as e:
        print("Unknown error ", e)
        microcontroller.reset() 