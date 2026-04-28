#ifndef EV_VERSION_H
#define EV_VERSION_H

#define EV_VERSION_MAJOR 0
#define EV_VERSION_MINOR 1
#define EV_VERSION_PATCH 0

/**
 * @brief Return the semantic version string of the framework snapshot.
 *
 * @return Constant string in MAJOR.MINOR.PATCH format.
 */
const char *ev_version_string(void);

#endif /* EV_VERSION_H */
