#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define DATA_TIMER 2000
#define MAX_SW 15      //发送窗口大小
#define ACK_TIMER 300 //ack计时器

struct FRAME {
	unsigned char kind; /* FRAME_DATA */
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int  padding;
};

static unsigned char frame_nr = 0, buffer[MAX_SW + 1][PKT_LEN], nbuffered = 0; //frame_nr目前的帧  buffer缓冲区 nb目前缓冲区
static unsigned char frame_expected = 0, ack_expected = 0; //希望收到的帧序号 ack
static int phl_ready = 0;
static unsigned char next_frame = 0; //下一个要发送的帧序号
int packet_length[MAX_SW + 1];

int f_between(unsigned char a, unsigned char b, unsigned char c) //检测帧在不在当前窗口
{
	if (((a <= b) && (b < c)) || ((c < a) && (a <= b)) ||((b < c) && (c < a)))
		//a:ack_expected,b:f.ack,c:next_frame
		return 1;
	else
		return 0;
}

static void put_frame(unsigned char *frame, int len)
{
	*(unsigned int *)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

static void send_data_frame(void)
{
	struct FRAME s;

	s.kind = FRAME_DATA;
	s.seq = next_frame;
	s.ack = (frame_expected+ MAX_SW) % (MAX_SW + 1);

	memcpy(s.data, buffer[next_frame], PKT_LEN);
	dbg_frame("Send DATA %d %d, ID %d windows %d\n", s.seq, s.ack, *(short *)s.data, nbuffered);
	put_frame((unsigned char *)&s, 3 + PKT_LEN);
	start_timer(next_frame, DATA_TIMER);
}

static void send_ack_frame(unsigned char fe)
{
	struct FRAME s;

	s.kind = FRAME_ACK;
	s.ack = (fe + MAX_SW) % (MAX_SW + 1);

	dbg_frame("Send ACK  %d\n", s.ack);

	put_frame((unsigned char *)&s, 2);
}

static void send_nak_frame(unsigned char fe)
{
	struct FRAME s;

	s.kind = FRAME_NAK;
	s.ack = (fe + MAX_SW) % (MAX_SW + 1);

	dbg_frame("Send NAK  %d\n", s.ack);

	put_frame((unsigned char *)&s, 2);
}

int main(int argc, char **argv)
{
	int event, arg;
	struct FRAME f;
	int len = 0;

	protocol_init(argc, argv);
	lprintf("Designed by Jiang Yanjun, build: " __DATE__ "  "__TIME__"\n");
	lprintf("杨书涵 胡圣椿 计网 2022 lab1\n");

	disable_network_layer();
	int nak_ = 1;
	for (;;)
	{
		event = wait_for_event(&arg);
		

		switch (event) {
		case NETWORK_LAYER_READY:
			get_packet(buffer[next_frame]);
			nbuffered++;
			send_data_frame();

			if (next_frame < MAX_SW) //
				next_frame++;
			else
				next_frame = 0;

			break;

		case PHYSICAL_LAYER_READY:
			phl_ready = 1;
			break;

		case FRAME_RECEIVED:
			len = recv_frame((unsigned char *)&f, sizeof f);

			if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				if (nak_) 
				{
					send_nak_frame(frame_expected);
					nak_ = 1;
					stop_ack_timer();
				}
				break;
			}

			if (f.kind == FRAME_ACK) //收到ack 不用操作
				dbg_frame("Recv ACK  %d\n", f.ack);

			if (f.kind == FRAME_NAK) //收到nak 重传对应帧
				dbg_frame("Recv NAK  %d\n", f.ack);

			if (f.kind == FRAME_DATA) //收到数据 判断是不是所期望的 是：开始ack并传输 不是：发送nak
			{
				dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short*)f.data);
				if (f.seq == frame_expected)
				{
					put_packet(f.data, len - 7);
					nak_ = 1;

					if (frame_expected < MAX_SW) //
						frame_expected++;
					else
						frame_expected = 0;

					start_ack_timer(ACK_TIMER);
				}
				else if (nak_)
				{
					send_nak_frame(frame_expected);
					nak_ = 0;
					stop_ack_timer();
				}
			}

			while (f_between(ack_expected, f.ack, next_frame)) 
			{
				nbuffered = nbuffered - 1;
				stop_timer(ack_expected);

				if (ack_expected < MAX_SW) //
					ack_expected++;
				else
					ack_expected = 0;
			}
			if (f.kind == FRAME_NAK) //重传开始
			{
				stop_timer(ack_expected + 1);
				next_frame = ack_expected;
				for (int i = 0; i < nbuffered; i++) 
				{
					send_data_frame();
					start_timer(next_frame, DATA_TIMER);
					stop_ack_timer();

					if (next_frame < MAX_SW) //
						next_frame++;
					else
						next_frame = 0;

				}
				phl_ready = 0;
			}

			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			next_frame = ack_expected;
			for (int i = 1; i <= nbuffered; i++) 
			{
				send_data_frame();
				start_timer(next_frame, DATA_TIMER);
				stop_ack_timer();

				if (next_frame < MAX_SW) //
					next_frame++;
				else
					next_frame = 0;
			}
			phl_ready = 0;
			break;

		case ACK_TIMEOUT:
			send_ack_frame(frame_expected);
			stop_ack_timer();
			break;
		}

		if (nbuffered < MAX_SW && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}