# Custom layout: registers left, source right, cmd bottom
tui new-layout srcreg {-horizontal src 2 regs 1} 3 status 0 cmd 1
tui new-layout asmreg {-horizontal asm 2 regs 1} 3 status 0 cmd 1

# Auto-activate on start
set trace-commands on
set logging file gdb.log
set logging enabled on
tui enable
layout srcreg
focus cmd
