//-------------------------------------------------------------------------
//                                                                       --
//                                                                       --
//      For use with ECE 385 Lab 62                                       --
//      UIUC ECE Department                                              --
//-------------------------------------------------------------------------


module synthesizer (

      ///////// Clocks /////////
      input     MAX10_CLK1_50, 

      ///////// KEY /////////
      input    [ 1: 0]   KEY,

      ///////// SW /////////
      input    [ 9: 0]   SW,

      ///////// LEDR /////////
      output   [ 9: 0]   LEDR,

      ///////// HEX /////////
      output   [ 7: 0]   HEX0,
      output   [ 7: 0]   HEX1,
      output   [ 7: 0]   HEX2,
      output   [ 7: 0]   HEX3,
      output   [ 7: 0]   HEX4,
      output   [ 7: 0]   HEX5,

      ///////// SDRAM /////////
      output             DRAM_CLK,
      output             DRAM_CKE,
      output   [12: 0]   DRAM_ADDR,
      output   [ 1: 0]   DRAM_BA,
      inout    [15: 0]   DRAM_DQ,
      output             DRAM_LDQM,
      output             DRAM_UDQM,
      output             DRAM_CS_N,
      output             DRAM_WE_N,
      output             DRAM_CAS_N,
      output             DRAM_RAS_N,

      ///////// VGA /////////
      output             VGA_HS,
      output             VGA_VS,
      output   [ 3: 0]   VGA_R,
      output   [ 3: 0]   VGA_G,
      output   [ 3: 0]   VGA_B,


      ///////// ARDUINO /////////
      inout    [15: 0]   ARDUINO_IO,
      inout              ARDUINO_RESET_N 

);




logic Reset_h, vssig, blank, sync, VGA_Clk;


//=======================================================
//  REG/WIRE declarations
//=======================================================
	logic SPI0_CS_N, SPI0_SCLK, SPI0_MISO, SPI0_MOSI, USB_GPX, USB_IRQ, USB_RST;
	logic [3:0] hex_num_4, hex_num_3, hex_num_1, hex_num_0; //4 bit input hex digits
	logic [1:0] signs;
	logic [1:0] hundreds;
	logic [7:0] Red, Blue, Green;
	logic [7:0] keycode;

//=======================================================
//  Structural coding
//=======================================================
	assign ARDUINO_IO[10] = SPI0_CS_N;
	assign ARDUINO_IO[13] = SPI0_SCLK;
	assign ARDUINO_IO[11] = SPI0_MOSI;
	assign ARDUINO_IO[12] = 1'bZ;
	assign SPI0_MISO = ARDUINO_IO[12];
	
	assign ARDUINO_IO[9] = 1'bZ; 
	assign USB_IRQ = ARDUINO_IO[9];
		
	//Assignments specific to Circuits At Home UHS_20
	assign ARDUINO_RESET_N = USB_RST;
	assign ARDUINO_IO[7] = USB_RST;//USB reset 
	assign ARDUINO_IO[8] = 1'bZ; //this is GPX (set to input)
	assign USB_GPX = 1'b0;//GPX is not needed for standard USB host - set to 0 to prevent interrupt
	
	//Assign uSD CS to '1' to prevent uSD card from interfering with USB Host (if uSD card is plugged in)
	assign ARDUINO_IO[6] = 1'b1;
	
	
	//Assign one button to reset
	assign {Reset_h}=~ (KEY[0]);

	//Our A/D converter is only 12 bit
	
	/* ############################ I2C Stuff ############################ */
	
	/* Generating 12.5 MHz master clock for the SGTL5000 */
	logic [1:0] audio_mclk_ctr;
	assign ARDUINO_IO[3] = audio_mclk_ctr[1];
	
	always_ff @(posedge MAX10_CLK1_50) begin
		audio_mclk_ctr <= audio_mclk_ctr + 1;
	end
	
	/* Control signals for I2C protocol so NIOS can communicate with SGTL5000 */
	logic i2c_scl_in, i2c_scl_oe, i2c_sda_in, i2c_sda_oe;
	
	assign i2c_scl_in = ARDUINO_IO[15];
	assign ARDUINO_IO[15] = i2c_scl_oe ? 1'b0 : 1'bz;
	
	assign i2c_sda_in = ARDUINO_IO[14];
	assign ARDUINO_IO[14] = i2c_sda_oe ? 1'b0 : 1'bz;
	
	
	
	/* ############################ Wave Generation etc. ############################ */
	
	logic [23:0] sample_0, sample_1, sample_2, sample_3, output_sample;	//connections for generating and mixing samples
	logic [15:0] note_vol_0, note_vol_1, note_vol_2, note_vol_3;			//connection to NIOS-II
	logic [7:0] reverb, master_vol, vibrato;
	
	waveform_generator note0( .clk(MAX10_CLK1_50), .reset(0), .wave_select(SW[1:0]), .vibrato(vibrato), .note_vol(note_vol_0), .sample(sample_0));
	waveform_generator note1( .clk(MAX10_CLK1_50), .reset(0), .wave_select(SW[1:0]), .vibrato(vibrato), .note_vol(note_vol_1), .sample(sample_1));
	waveform_generator note2( .clk(MAX10_CLK1_50), .reset(0), .wave_select(SW[1:0]), .vibrato(vibrato), .note_vol(note_vol_2), .sample(sample_2));
	waveform_generator note3( .clk(MAX10_CLK1_50), .reset(0), .wave_select(SW[1:0]), .vibrato(vibrato), .note_vol(note_vol_3), .sample(sample_3));
	
	mixer mix(.clk(MAX10_CLK1_50), .sample_clock(ARDUINO_IO[4]), .reverb(reverb), .master_vol(master_vol), .sample_0(sample_0), .sample_1(sample_1), .sample_2(sample_2), .sample_3(sample_3), .mixed_sample(output_sample));

	/* Display last note played on hex displays */
	logic [3:0] note_name, octave, partial;
	note_table notes(.MIDI_freq(note_vol_0[14:8]), .note_name, .octave, .partial);
	
	HexDriver hex_driver2 (note_name, HEX2[6:0]);
	assign HEX2[7] = 1'b1;
	
	HexDriver hex_driver1 (partial, HEX1[6:0]);
	assign HEX1[7] = 1'b1;
	
	HexDriver hex_driver0 (octave, HEX0[6:0]);
	assign HEX0[7] = 1'b1;	

	/* Smaller I2S samples for use with FFT module's slower sample rate */
	logic small_data, small_LR, small_SCLK;
	small_I2S_interface small_boi( .LRCLK(small_LR), .SCLK(small_SCLK), .data_in(output_sample >> 6), .SDATA(small_data) );
	
	/* Module for drawing crude 16-element FFT on the display */
	FFT fft_VGA( .clk(MAX10_CLK1_50), .reset(KEY[0]), .DOUT(small_data), 
					 .LRCLK(small_LR), .BCLK(small_SCLK), .vsync(VGA_VS), .hsync(VGA_HS), .r(VGA_R), .g(VGA_G), .b(VGA_B));

	
	/* Actual I2S protocol wired to the SGTL5000 IO pins */
	I2S_interface i2s( .LRCLK(ARDUINO_IO[4]), .SCLK(ARDUINO_IO[5]), .data_in(output_sample), .SDATA(ARDUINO_IO[2]) );
	
	/* SOC instantiation */
	synthesizer_soc (
		.clk_clk                           (MAX10_CLK1_50),  //clk.clk
		.reset_reset_n                     (1'b1),           //reset.reset_n
		.altpll_0_locked_conduit_export    (),               //altpll_0_locked_conduit.export
		.altpll_0_phasedone_conduit_export (),               //altpll_0_phasedone_conduit.export
		.altpll_0_areset_conduit_export    (),               //altpll_0_areset_conduit.export
		.key_external_connection_export    (KEY),            //key_external_connection.export
		
      //I2C
		.i2c0_sda_in(i2c_sda_in),                    //i2c0.sda_in
		.i2c0_scl_in(i2c_scl_in),                    //i2c0.scl_in
		.i2c0_sda_oe(i2c_sda_oe),                    //i2c0.sda_oe
		.i2c0_scl_oe(i2c_scl_oe),                    //i2c0.scl_oe
		
		//MIDI
		.note_vol_0_export(note_vol_0),
		.note_vol_1_export(note_vol_1),
		.note_vol_2_export(note_vol_2),
		.note_vol_3_export(note_vol_3),
		.master_vol_export(master_vol),
		.reverb_export(reverb),
		.vibrato_export(vibrato),
		
		//SDRAM
		.sdram_clk_clk(DRAM_CLK),                            //clk_sdram.clk
		.sdram_wire_addr(DRAM_ADDR),                         //sdram_wire.addr
		.sdram_wire_ba(DRAM_BA),                             //.ba
		.sdram_wire_cas_n(DRAM_CAS_N),                       //.cas_n
		.sdram_wire_cke(DRAM_CKE),                           //.cke
		.sdram_wire_cs_n(DRAM_CS_N),                         //.cs_n
		.sdram_wire_dq(DRAM_DQ),                             //.dq
		.sdram_wire_dqm({DRAM_UDQM,DRAM_LDQM}),              //.dqm
		.sdram_wire_ras_n(DRAM_RAS_N),                       //.ras_n
		.sdram_wire_we_n(DRAM_WE_N),                         //.we_n

		//USB SPI	
		.spi0_SS_n(SPI0_CS_N),
		.spi0_MOSI(SPI0_MOSI),
		.spi0_MISO(SPI0_MISO),
		.spi0_SCLK(SPI0_SCLK),
		
		//USB GPIO
		.usb_rst_export(USB_RST),
		.usb_irq_export(USB_IRQ),
		.usb_gpx_export(USB_GPX),
		
		//LEDs and HEX
		.hex_digits_export({hex_num_4, hex_num_3, hex_num_1, hex_num_0}),
		.leds_export({hundreds, signs, LEDR}),
		.keycode_export(keycode)
		
	 );

endmodule
