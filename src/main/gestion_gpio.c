#include <gpiod.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>

#include "gestion_gpio.h"

#define GPIO_CHIP "gpiochip2"


//fonction read :
int8_t gpio_read_input_value(int8_t GPIO_LINE_IN ){

    int8_t ret;
    struct gpiod_chip *chip = NULL;
    struct gpiod_line *line_in = NULL;

    // Ouvrir le chip GPIO
    chip = gpiod_chip_open_by_name(GPIO_CHIP);
    if (!chip) {
        perror("Erreur ouverture gpiochip");
        return -1;
    }

    // Obtenir la ligne GPIO pour l'entree
    line_in = gpiod_chip_get_line(chip, GPIO_LINE_IN);
    if (!line_in) {
        perror("Erreur récupération ligne GPIO");
        gpiod_chip_close(chip);
        return -1;
    }

    // Configurer en entree
    if (gpiod_line_request_input(line_in, "gpio_read_input_value") < 0) {
        perror("Erreur configuration entree");
        gpiod_chip_close(chip);
        return -1;
    }

    ret =!gpiod_line_get_value(line_in); // lorsque on active l'entree elle donne un etat bas donc il faut inverser avec !

    gpiod_line_release(line_in);
    gpiod_chip_close(chip);

    return ret;
}

int8_t gpio_write_output_value(int8_t GPIO_LINE_OUT , int8_t value ){

    GPIO_LINE_OUT += 8;

    struct gpiod_chip *chip = NULL;
    struct gpiod_line *line_out = NULL;

    // Ouvrir le chip GPIO
    chip = gpiod_chip_open_by_name(GPIO_CHIP);
    if (!chip) {
        perror("Erreur ouverture gpiochip");
        return -1;
    }

    // Obtenir la ligne GPIO pour la sortie
    line_out = gpiod_chip_get_line(chip, GPIO_LINE_OUT);
    if (!line_out) {
        perror("Erreur récupération ligne GPIO");
        gpiod_chip_close(chip);
        return -1;
    }

    // Configurer en sortie
    if (gpiod_line_request_output(line_out, "test_input", 0) < 0) {
        perror("Erreur configuration sortie");
        gpiod_chip_close(chip);
        return -1;
    }


    gpiod_line_set_value(line_out, value);

    gpiod_line_release(line_out);
    gpiod_chip_close(chip);

    return 0;
}
