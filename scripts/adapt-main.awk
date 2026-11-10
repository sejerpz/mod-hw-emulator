# mod-hw-emulator — build adaptations of the firmware main.c (applied to a copy in the build tree)
#
# - main() becomes fw_main(): the board starts it on its own thread
# - the ARM fault analysis and handlers read core registers (SCB, LR): cut out, from HardFault_Analisys (or
#   HardFault_Handler, on firmwares without the analysis) to the idle hook (HardFault, MemManage, BusFault, UsageFault, malloc failed hook)
# - the idle hook of the firmware is a fatal error trap; the host port has its own idle hook
# - the CCC_ANALYZER link stubs clash with the symbols the emulator provides
#
# Candidates for `#ifdef MOD_HW_EMULATOR` guards upstream.

/^int main\(void\)/ { sub(/main/, "fw_main") }

/^void HardFault_(Analisys|Handler)\(/ { skipping = 1 }
/^void vApplicationIdleHook\(void\)/ { skipping = 0; sub(/vApplicationIdleHook/, "fw_vApplicationIdleHook") }

/^#ifdef CCC_ANALYZER/ { stubs = 1 }
stubs { if (/^#endif/) stubs = 0; next }

!skipping { print }
