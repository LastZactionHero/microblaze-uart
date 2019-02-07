connect -url tcp:localhost:3121
configparams mdm-detect-bscan-mask 2
targets -set -nocase -filter {name =~ "microblaze*#0" && bscan=="USER2"  && jtag_cable_name =~ "Digilent Arty 210319A7664EA"} -index 0
rst -processor
targets -set -nocase -filter {name =~ "microblaze*#0" && bscan=="USER2"  && jtag_cable_name =~ "Digilent Arty 210319A7664EA"} -index 0
dow /root/Development/microblaze_uart/microblaze_uart.sdk/uart_interrupts/Debug/uart_interrupts.elf
bpadd -addr &main
