/**
 * @file sanitizer.h
 * 
 * @brief HTTP request sanitizer.
 * 
 * This file serves as a template for creating new header files in the project.
 * It includes standard sections for includes, defines, enumerated types,
 * structured types, variables, and function declarations.
 * 
 * @author  Roman Horshkov <github.com/RomanHorshkov>
 * @date    dec 2025
 * (c) 2025
 */
#ifndef SERVER_HTTP_SANITIZER_H
#define SERVER_HTTP_SANITIZER_H

/****************************************************************************
 * INCLUDES
 ****************************************************************************
 */

#include "http_common.h" /* http_request, sv_t */

/****************************************************************************
 * DEFINES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * ENUMERATED TYPEDEFS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * ENUMERATED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * STRUCTURED TYPEDEFS
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * STRUCTURED VARIABLES
 ****************************************************************************
 */
/* None */

/****************************************************************************
 * PUBLIC FUNCTIONS DECLARATIONS
 ****************************************************************************
 */
int sanitize_http_request(http_request_t *req);


#endif /* SERVER_HTTP_SANITIZER_H */
