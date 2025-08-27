library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity spi_master_axis is
    generic(
        M_AXIS_TDATA_WIDTH : integer := 32;
        M_SPI_TRANSFER_LENGTH : integer := 16;
        M_SPI_SAMPLE_LENGTH : integer := 16
    );
    port(
        --Master out serial clock
        M_O_SCLK : out std_logic := '1';
        --Master in miso
        M_I_MISO : in std_logic := '0';
        --Master out slave select
        M_O_SS : out std_logic := '1';
        --Master out ADC data low bits
        M_O_RX_DATA : out std_logic_vector(7 downto 0) := (others => '0');

    --AXIS Master ports
        --Master clock    
        M_AXIS_ACLK : in std_logic;
        --Master reset
        M_AXIS_ARESETN : in std_logic;
        --Master valid out
        M_AXIS_TVALID : out std_logic := '0';
        --Master ready out
        M_AXIS_TREADY : in std_logic;
        --Master data out
        M_AXIS_TDATA : out std_logic_vector(M_AXIS_TDATA_WIDTH-1 downto 0) := (others => '0');
        --Master last packet out
        M_AXIS_TLAST : out std_logic := '0'
    );
end spi_master_axis;

architecture RTL of spi_master_axis is

--Internal signal declaration
    --Slave select
    signal o_ss : std_logic := '1';
    --Master input miso
    signal i_miso : std_logic;
    --Master output serial clock (12.5 MHz)
    signal o_sclk : std_logic;
    --Serial clock counter
    signal sclk_counter : std_logic_vector(2 downto 0) := (others => '0');
    signal prev_sclk_counter : std_logic_vector(2 downto 0) := (others => '0');

    --Sample clock (44.1 kHz)
    signal sample_pulse : std_logic;
    signal sample_pulse_counter : integer range 0 to 2268 := 0;

    --Start SPI sample
    --Begins on sample_pulse rising edge
    signal do_spi_sample : std_logic := '0';
    --SPI sample, we expect 4 leading 0's and 12 bits of data from PmodMIC3 ADC
    signal spi_sample : std_logic_vector(15 downto 0) := (others => '0');
    --Bit selector for sample storing
    signal spi_bit_counter : integer range 0 to 15 := 15;

    --We want to send an AXIS packet of 16 SPI samples
    type spi_sample_array is array (0 to M_SPI_TRANSFER_LENGTH-1) of std_logic_vector(15 downto 0);
    signal spi_samples : spi_sample_array := (others => (others => '0'));
    --Spi whole sample of 16 bits counter
    signal spi_whole_sample_count : integer range 0 to M_SPI_TRANSFER_LENGTH-1 := 0;
    --
    signal write_whole_sample_count : integer range 0 to M_SPI_TRANSFER_LENGTH-1 := 0;

    --Spi samples is filled with M_SPI_TRANSFER_LENGTH samples
    --Transfer samples over AXIS
    signal do_transfer : std_logic := '0';
    --Sample counter for transfer
    signal transfer_sample_counter : integer range 0 to M_SPI_SAMPLE_LENGTH-1 := 0;
    signal prev_transfer_sample_counter : integer range 0 to M_SPI_SAMPLE_LENGTH-1 := 0;
    --Transfered all stored samples
    signal transfer_completed : std_logic := '0';

    --This tvalid for use in process
    signal this_tvalid : std_logic := '0';
    --Next tvalid for assignment in a process
    signal next_tvalid : std_logic := '0';
    --This tdata for use in process
    signal this_tdata : std_logic_vector(M_AXIS_TDATA_WIDTH-1 downto 0) := (others => '0');
    --Next tdata for assignment in a process
    signal next_tdata : std_logic_vector(M_AXIS_TDATA_WIDTH-1 downto 0) := (others => '0');

    --testing signal
    signal write_sample_condition : std_logic := '0';

begin

    --I/O assignments
    i_miso <= M_I_MISO;
    M_O_SS <= o_ss;
    M_O_SCLK <= o_sclk;

    --Output received data to leds
    M_O_RX_DATA <= this_tdata(15 downto 8);

    --Internal signal use
    M_AXIS_TDATA <= this_tdata;

    --Sample clock signal
    sample_pulse <= '1' when (sample_pulse_counter = 50) else '0';

    --Set TLAST when last SPI sample is assigned to TDATA
    M_AXIS_TLAST <= '1' when (write_whole_sample_count = 15 and spi_whole_sample_count = 0 and this_tvalid = '1') else '0';

    --Sample clock counter process
    sample_pulse_process : process(M_AXIS_ACLK)
    begin
        if(rising_edge(M_AXIS_ACLK)) then
            if(M_AXIS_ARESETN = '0') then
                sample_pulse_counter <= 0;
            elsif(sample_pulse_counter < 2268) then
                sample_pulse_counter <= sample_pulse_counter + 1;
            else
                sample_pulse_counter <= 0;
            end if;
        end if;
    end process sample_pulse_process;

    --Sample clock sets do_spi_sample
    do_spi_sample_process : process(M_AXIS_ACLK)
    begin
        if(rising_edge(M_AXIS_ACLK)) then
            if(M_AXIS_ARESETN = '0') then
                do_spi_sample <= '0';
            else
                if(sample_pulse = '1') then
                    do_spi_sample <= '1';
                elsif(spi_bit_counter = 0 and sclk_counter = "011" and prev_sclk_counter = "010") then
                    do_spi_sample <= '0';
                end if;
            end if;
        end if;
    end process do_spi_sample_process;

    --Sample collection
    write_whole_sample_count <= (spi_whole_sample_count-1) when (spi_whole_sample_count > 0) else 15;
    write_sample_condition <= '1' when (sclk_counter = "100" and prev_sclk_counter = "011" and do_spi_sample = '0') else '0';

    spi_samples(write_whole_sample_count) <= spi_sample when (write_sample_condition = '1');

    --Sample collection process
    --do_transfer samples over AXIS assignment
    sampling_process : process(M_AXIS_ACLK)
    begin
        if(rising_edge(M_AXIS_ACLK)) then
            if(M_AXIS_ARESETN = '0') then
                spi_sample <= (others => '0');
                spi_bit_counter <= 15;
            elsif(sclk_counter = "011" and prev_sclk_counter = "010") then
                if(spi_bit_counter > 0 and do_spi_sample = '1') then
                    spi_sample(spi_bit_counter) <= i_miso;
                    spi_bit_counter <= spi_bit_counter - 1;
                elsif(do_spi_sample = '1') then
                    --Last bit of sample stored
                    spi_sample(spi_bit_counter) <= i_miso;
                    spi_bit_counter <= 15;
                    --One sample of 16 bits done
                    if(spi_whole_sample_count < M_SPI_TRANSFER_LENGTH-1) then
                        spi_whole_sample_count <= spi_whole_sample_count + 1;
                        do_transfer <= '0';
                    else
                        spi_whole_sample_count <= 0;
                        do_transfer <= '1';
                    end if;
                end if;
            end if;
        end if;
    end process sampling_process;

    --next_tvalid assignment
    --next_tvalid <= '1' when (do_transfer = '1' and transfer_completed = '0') else '0';

    --Valid comes as soon as we get the whole sample from SPI
    next_tvalid <= '1' when (write_sample_condition = '1') else '0';

    --SPI sample array to next_tdata assignment
    --next_tdata <= x"0000" & spi_samples(transfer_sample_counter) when (next_tvalid = '1') else x"DEADBEEF";

    --Send spi_sample as soon as it is sampled
    next_tdata <= x"0000" & spi_sample when (next_tvalid = '1') else x"DEADBEEF";

    do_transfer_process : process(M_AXIS_ACLK)
    begin
        if(rising_edge(M_AXIS_ACLK)) then
            if(M_AXIS_ARESETN = '0') then
                transfer_sample_counter <= 0;
                prev_transfer_sample_counter <= 0;
                transfer_completed <= '0';
            elsif(do_transfer = '1' and transfer_completed = '0' and M_AXIS_TREADY = '1') then
                if(transfer_sample_counter < M_SPI_SAMPLE_LENGTH-1) then
                    prev_transfer_sample_counter <= transfer_sample_counter;
                    transfer_sample_counter <= transfer_sample_counter + 1;
                else
                    prev_transfer_sample_counter <= transfer_sample_counter;
                    transfer_sample_counter <= 0;
                    transfer_completed <= '1';
                end if;
            elsif(transfer_sample_counter = 0 and do_transfer = '0') then
                transfer_completed <= '0';
            end if;
        end if;
    end process do_transfer_process;

    --Slave select assignment process
    slave_select_process : process(M_AXIS_ACLK)
    begin
        if(rising_edge(M_AXIS_ACLK)) then
            if(M_AXIS_ARESETN = '0') then
                o_ss <= '1';
            elsif(do_spi_sample = '1' or sample_pulse = '1') then
                o_ss <= '0';
            else
                o_ss <= '1';
            end if;
        end if;
    end process slave_select_process;

    --TVALID assignment process
    axis_tvalid_process : process(M_AXIS_ACLK)
    begin
        if(rising_edge(M_AXIS_ACLK)) then
            if(M_AXIS_ARESETN = '0') then
                this_tvalid <= '0';
                M_AXIS_TVALID <= '0';
            elsif(this_tvalid = '0' or M_AXIS_TREADY = '1') then
                this_tvalid <= next_tvalid;
                M_AXIS_TVALID <= next_tvalid;
            end if;
        end if;
    end process axis_tvalid_process;

    --TDATA assignment process
    axis_tdata_process : process(M_AXIS_ACLK)
    begin
        if(rising_edge(M_AXIS_ACLK)) then
            if(M_AXIS_ARESETN = '0') then
                this_tdata <= (others => '0');
            elsif(this_tvalid = '0' or M_AXIS_TREADY = '1') then
                this_tdata <= next_tdata;
                if(next_tvalid = '0') then
                    this_tdata <= (others => '0');
                end if;
            end if;
        end if;
    end process axis_tdata_process;

    --Serial clock assignment
    o_sclk <= '0' when (sclk_counter < "100" and o_ss = '0') else '1';
    --Serial clock counter process
    sclk_counter_process : process(M_AXIS_ACLK)
    begin
        if(rising_edge(M_AXIS_ACLK)) then
            if(M_AXIS_ARESETN = '0') then
                prev_sclk_counter <= (others => '0');
                sclk_counter <= (others => '0');
            elsif(do_spi_sample = '1') then
                prev_sclk_counter <= sclk_counter;
                sclk_counter <= std_logic_vector(unsigned(sclk_counter) + 1);
            else
                sclk_counter <= (others => '0');
                prev_sclk_counter <= (others => '0');
            end if;
        end if;
    end process sclk_counter_process;
end RTL;