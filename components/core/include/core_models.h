#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // for size_t

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SEX_UNKNOWN = 0,
    SEX_MALE,
    SEX_FEMALE
} animal_sex_t;

typedef struct {
    uint32_t date;
    float value;
    char unit[8]; // "g", "kg"
} weight_record_t;

typedef enum {
    EVENT_FEEDING = 0,
    EVENT_SHEDDING,
    EVENT_VET,
    EVENT_CLEANING,
    EVENT_MATING,   // Accouplement
    EVENT_LAYING,   // Ponte
    EVENT_HATCHING, // Eclosion
    EVENT_OTHER
} event_type_t;

typedef struct {
    uint32_t date;
    event_type_t type;
    char description[64];
} event_record_t;

typedef struct {
    char id[37];            // UUID
    char name[64];          // Nom/Numéro d'usage
    char species[128];      // Nom scientifique
    animal_sex_t sex;       // 'M', 'F', 'U'
    uint32_t dob;           // Timestamp naissance (0 si inconnu)
    char origin[16];        // NC, WC, CB...
    char registry_id[32];   // Numéro I-FAP / Registre
    bool is_deleted;        // Soft delete
    
    // Dynamic Lists
    weight_record_t *weights;
    size_t weight_count;
    
    event_record_t *events;
    size_t event_count;
} animal_t;

// Lightweight structure for listing
typedef struct {
    char id[37];
    char name[64];
    char species[128];
} animal_summary_t;

typedef struct {
    char id[37];            // UUID
    char type[32];          // CITES, Cession, Facture...
    char ref_number[64];    // Numéro du document
    uint32_t date_issued;   // Date d'émission
    uint32_t date_expire;   // Date d'expiration (0 si permanent)
    char linked_animal_id[37]; // FK vers Animal
} document_t;

typedef enum {
    LOG_LEVEL_INFO = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_AUDIT
} log_level_t;

typedef struct {
    uint32_t timestamp;
    log_level_t level;
    char module[16];        // "CORE", "NET", "SYS"
    char message[128];      // Description courte
} log_entry_t;

#ifdef __cplusplus
}
#endif