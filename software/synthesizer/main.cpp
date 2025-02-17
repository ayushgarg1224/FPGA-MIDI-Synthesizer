#include "include/hidboot.h"
#include "include/usbhub.h"
#include "include/usbh_midi.h"
// Satisfy the IDE, which needs to see the include statment in the ino too.
#ifdef dobogusinclude
#include <spi4teensy3.h>
#endif
#include "include/SPI.h"
#include <ctime>

void test_loop();
void press_any_key();
void print_hex(int v, int num_places);
void halt55();

/* USB Host Shield 2.0 board quality control routine */
/* To see the output set your terminal speed to 115200 */
/* for GPIO test to pass you need to connect GPIN0 to GPOUT7, GPIN1 to GPOUT6, etc. */
/* otherwise press any key after getting GPIO error to complete the test */
/**/

/* variables */
uint8_t rcode;
uint8_t usbstate;
uint8_t laststate;
//uint8_t buf[sizeof(USB_DEVICE_DESCRIPTOR)];
USB_DEVICE_DESCRIPTOR buf;
USB Usb;


void toBinary(uint8_t a)
{
    uint8_t i;

    for(i=0x80;i!=0;i>>=1)
        printf("%c",(a&i)?'1':'0');
}

extern "C" {
	#include "sgtl5000_test.h"
}

int main() {
	printf("Initializing SGTL5000...\n");
	initialize_sgtl5000();
	printf("Initializing MIDI connection...\n");
	USBH_MIDI Midi(&Usb);
	if(Usb.Init() == -1){
		printf("Halted...");
		while(1);
	}
	delay(200);

	/* Pointers to PIOs */
	int NUM_NOTES = 4;

	volatile unsigned int* note_vol_array[NUM_NOTES] = {(unsigned int*)0x08001200, (unsigned int*)0x080011f0, (unsigned int*)0x080011e0, (unsigned int*)0x080011d0};
	volatile unsigned int* master_vol = (unsigned int*)0x08001220;
	volatile unsigned int* reverb = (unsigned int*)0x08001210;
	volatile unsigned int* vibrato = (unsigned int*)0x080011c0;
	unsigned int decay_time, release_time, sustain_level, vol, ms_to_dec;

	/* Initialize effects */
	*(master_vol) = 64;
	*(vibrato) = 64;
	*(reverb) = 0;
	release_time = 0;
	decay_time = 0;
	sustain_level = 127;

	for(int i = 0; i < NUM_NOTES; i++)
			*(note_vol_array[i]) = 0;

	clock_t note_clocks[NUM_NOTES];
	int available_idx;
	bool note_used[NUM_NOTES] = {false};
	bool note_released[NUM_NOTES] = {false};
	bool first_note = true;
	bool muted = false;
	bool foundNote = false;

	while(1){
		Usb.Task();
		if(Midi){
			uint8_t MIDI_packet[ 3 ];
			uint8_t size;

			do {
				if ( (size = Midi.RecvData(MIDI_packet)) > 0 ) {

					switch(unsigned(MIDI_packet[0] >> 4)){
					case 9:						//Note ON
						if(first_note){			//handles an initial data packet that gets interpreted as note on event
							first_note = false;
							break;
						}

						if(!muted){	//check muted flag
							/* Find first available note_vol */
							available_idx = -1;
							for(int i = 0; i < NUM_NOTES; i++){
								if(note_released[i] && ((*(note_vol_array[i]) >> 8) == unsigned(MIDI_packet[1]))){	//if note is decaying after release
									available_idx = i;
									note_used[i] = true;
									note_released[i] = false;
									foundNote = true;
									break;
								}
							}
							if(!foundNote){
								for(int i = 0; i < NUM_NOTES; i++){
									if(!note_used[i]){
										available_idx = i;
										note_used[i] = true;
										break;
									}
								}
							}
							foundNote = false;
							/* If a note_vol is available, write to it*/
							if(available_idx != -1){
								*(note_vol_array[available_idx]) = (MIDI_packet[1] << 8) + MIDI_packet[2];
								note_clocks[available_idx] = clock();
							}
						}
						break;

					case 8:		//Note OFF
						for(int i = 0; i < NUM_NOTES; i++){    								//iterate over all note_vols
							if((*(note_vol_array[i]) >> 8) == unsigned(MIDI_packet[1])){  	//we've found the note to turn off
								if(release_time != 0){
									note_released[i] = true;
									note_clocks[i] = clock();
								}
								else{
									*(note_vol_array[i]) = 0;                  					//note turned off
									note_used[i] = false;										//reset flag
								}
								break;
							}
						}
						break;

					case 0xE:	//Pitch wheel
						*(vibrato) = unsigned(MIDI_packet[2]);
						printf("Vibrato = %u\n", *vibrato);
						break;

					case 0xB:	//Additional effects - Byte 2: Volume wheel (01), knobs (14-17), buttons (30-33)

						switch(unsigned(MIDI_packet[1])){

						/* Volume wheel */
						case 0x01:
							*(master_vol) = unsigned(MIDI_packet[2]);
							printf("Master vol = %d\n", *master_vol);
							break;

						/* Knobs */
						case 0x14:	//Reverb
							*(reverb) = unsigned(MIDI_packet[2]);
							printf("Reverb = %d\n", *reverb);
							break;

						case 0x15:	//Release time
							release_time = unsigned(MIDI_packet[2]);
							printf("Release time = %d\n", release_time);
							break;

						case 0x16:	//Decay time
							ms_to_dec = unsigned(MIDI_packet[2]) + 1;
							printf("Decay time = %d\n", ms_to_dec);
							break;

						case 0x17:	//Sustain level
							sustain_level = unsigned(MIDI_packet[2]);
							printf("Sustain level = %u\n", sustain_level);
							break;

						/* Buttons */
						case 0x30:	//1: mute
							if(unsigned(MIDI_packet[2]) == 0x7F){	//button ON
								muted = true;
								for(int i = 0; i < NUM_NOTES; i++){
										*(note_vol_array[i]) = 0;
										note_used[i] = false;
								}
							}
							else
								muted = false;

							break;

						case 0x31:	//2
							break;
						case 0x32:	//3
							break;
						case 0x33:	//4
							break;
						}
						break;
					}
				}
			} while (size > 0);
		}
		for(int i = 0; i < NUM_NOTES; i++){
			/* Implement release effect */
			if(note_released[i]){
				vol = *(note_vol_array[i]) & 0x00FF;
				if(5000*(clock() - note_clocks[i])/CLOCKS_PER_SEC >= release_time){
					note_clocks[i] = clock();
					if(vol > 0)
						*(note_vol_array[i]) -= 1;
					else{
						*(note_vol_array[i]) = 0;
						note_used[i] = false;
						note_released[i] = false;
					}
				}
			}
			/* Implement decay effect */
			if(note_used[i]){							//if note is being played
				vol = *(note_vol_array[i]) & 0x00FF;	//volume is bottom 8 bits
				if((2000*(clock() - note_clocks[i])/CLOCKS_PER_SEC) >= ms_to_dec){	//if above sustain level, decrement volume

					note_clocks[i] = clock();					//update note clock
					if( (vol > sustain_level) && (vol > 0) )	//check to decrement volume
						*(note_vol_array[i]) -= 1;
					else if(vol == 0){										//turn note off if volume is zero
						*(note_vol_array[i]) = 0;
						note_used[i] = false;
					}
				}
			}
		}
	}
}


void test_loop() {
        delay(200);
        Usb.Task();
        usbstate = Usb.getUsbTaskState();
        if(usbstate != laststate) {
                laststate = usbstate;
                /**/
                switch(usbstate) {
                        case( USB_DETACHED_SUBSTATE_WAIT_FOR_DEVICE):
                                E_Notify(PSTR("\r\nWaiting for device..."), 0x80);
                                break;
                        case( USB_ATTACHED_SUBSTATE_RESET_DEVICE):
                                E_Notify(PSTR("\r\nDevice connected. Resetting..."), 0x80);
                                break;
                        case( USB_ATTACHED_SUBSTATE_WAIT_SOF):
                                E_Notify(PSTR("\r\nReset complete. Waiting for the first SOF..."), 0x80);
                                break;
                        case( USB_ATTACHED_SUBSTATE_GET_DEVICE_DESCRIPTOR_SIZE):
                                E_Notify(PSTR("\r\nSOF generation started. Enumerating device..."), 0x80);
                                break;
                        case( USB_STATE_ADDRESSING):
                                E_Notify(PSTR("\r\nSetting device address..."), 0x80);
                                break;
                        case( USB_STATE_RUNNING):
                                E_Notify(PSTR("\r\nGetting device descriptor"), 0x80);
                                rcode = Usb.getDevDescr(1, 0, sizeof (USB_DEVICE_DESCRIPTOR), (uint8_t*) & buf);

                                if(rcode) {
                                        E_Notify(PSTR("\r\nError reading device descriptor. Error code "), 0x80);
                                        print_hex(rcode, 8);
                                } else {
                                        /**/
                                        E_Notify(PSTR("\r\nDescriptor Length:\t"), 0x80);
                                        print_hex(buf.bLength, 8);
                                        E_Notify(PSTR("\r\nDescriptor type:\t"), 0x80);
                                        print_hex(buf.bDescriptorType, 8);
                                        E_Notify(PSTR("\r\nUSB version:\t\t"), 0x80);
                                        print_hex(buf.bcdUSB, 16);
                                        E_Notify(PSTR("\r\nDevice class:\t\t"), 0x80);
                                        print_hex(buf.bDeviceClass, 8);
                                        E_Notify(PSTR("\r\nDevice Subclass:\t"), 0x80);
                                        print_hex(buf.bDeviceSubClass, 8);
                                        E_Notify(PSTR("\r\nDevice Protocol:\t"), 0x80);
                                        print_hex(buf.bDeviceProtocol, 8);
                                        E_Notify(PSTR("\r\nMax.packet size:\t"), 0x80);
                                        print_hex(buf.bMaxPacketSize0, 8);
                                        E_Notify(PSTR("\r\nVendor  ID:\t\t"), 0x80);
                                        print_hex(buf.idVendor, 16);
                                        E_Notify(PSTR("\r\nProduct ID:\t\t"), 0x80);
                                        print_hex(buf.idProduct, 16);
                                        E_Notify(PSTR("\r\nRevision ID:\t\t"), 0x80);
                                        print_hex(buf.bcdDevice, 16);
                                        E_Notify(PSTR("\r\nMfg.string index:\t"), 0x80);
                                        print_hex(buf.iManufacturer, 8);
                                        E_Notify(PSTR("\r\nProd.string index:\t"), 0x80);
                                        print_hex(buf.iProduct, 8);
                                        E_Notify(PSTR("\r\nSerial number index:\t"), 0x80);
                                        print_hex(buf.iSerialNumber, 8);
                                        E_Notify(PSTR("\r\nNumber of conf.:\t"), 0x80);
                                        print_hex(buf.bNumConfigurations, 8);
                                        /**/
                                        E_Notify(PSTR("\r\n\nAll tests passed. Press RESET to restart test"), 0x80);
#ifdef ESP8266
                                                yield(); // needed in order to reset the watchdog timer on the ESP8266
#endif

                                }
                                break;
                        case( USB_STATE_ERROR):
                                E_Notify(PSTR("\r\nUSB state machine reached error state"), 0x80);
                                break;

                        default:
                                break;
                }//switch( usbstate...
        }
}//loop()...

/* constantly transmits 0x55 via SPI to aid probing */
void halt55() {

        E_Notify(PSTR("\r\nUnrecoverable error - test halted!!"), 0x80);
        E_Notify(PSTR("\r\n0x55 pattern is transmitted via SPI"), 0x80);
        E_Notify(PSTR("\r\nPress RESET to restart test"), 0x80);

        while(1) {
                Usb.regWr(0x55, 0x55);
#ifdef ESP8266
                yield(); // needed in order to reset the watchdog timer on the ESP8266
#endif
        }
}

/* prints hex numbers with leading zeroes */
void print_hex(int v, int num_places) {
        int mask = 0, n, num_nibbles, digit;

        for(n = 1; n <= num_places; n++) {
                mask = (mask << 1) | 0x0001;
        }
        v = v & mask; // truncate v to specified number of places

        num_nibbles = num_places / 4;
        if((num_places % 4) != 0) {
                ++num_nibbles;
        }
        do {
                digit = ((v >> (num_nibbles - 1) * 4)) & 0x0f;
                printf("%x\n", digit);
        } while(--num_nibbles);
}

/* prints "Press any key" and returns when key is pressed */
void press_any_key() {
        E_Notify(PSTR("\r\nPress any key to continue..."), 0x80);
//        char x;
//        scanf("%s", &x);
}
