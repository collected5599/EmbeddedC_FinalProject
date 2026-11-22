#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Set to 1 to select GPIO diagnostics. */
#define IO_DIAGNOSTICS_MODE 0

/* Set to 1 to run the guided ToF check and save its profile in QSPI sectors 1–2.
 * Keep IO_DIAGNOSTICS_MODE at 0 when enabling this mode. */
#define IO_TOF_CALIBRATION_MODE 0

#if IO_DIAGNOSTICS_MODE && IO_TOF_CALIBRATION_MODE
#error "Select only one diagnostic mode at a time"
#endif

#endif /* APP_CONFIG_H */
