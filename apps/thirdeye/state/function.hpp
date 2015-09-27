/*
 * function.hpp
 *
 *  Created on: Aug 19, 2015
 *      Author: bcurtis
 */

#include <stdint.h>
#include <iostream>
#include <iomanip>
#include <map>
#include <vector>

#ifndef FUNCTION_HPP
#define FUNCTION_HPP

namespace STATE {

class Functions {
    std::map<uint32_t, std::vector<uint8_t>> mMemory;

public:

    Functions();
    virtual ~Functions();


    // Miscellaneous functions
    void load_string(int32_t argcnt, int8_t *array, uint32_t string);
    void load_resource(int32_t argcnt, int8_t *array, uint32_t resource);
    void copy_string(int32_t argcnt, int8_t *src, int8_t *dest);
    void string_force_lower(int32_t argcnt, int8_t *dest);
    void string_force_upper(int32_t argcnt, int8_t *dest);
    uint32_t string_len(int32_t argcnt, int8_t *string);
    uint32_t string_compare(int32_t argcnt, int8_t *str1, int8_t *str2);
    void beep(void);
    int32_t strval(int32_t argcnt, int8_t *string);
    int32_t envval(int32_t argcnt, int8_t *name);
    void pokemem(std::map<uint8_t, std::vector<uint8_t>> &parameters); //void pokemem(int32_t argcnt, int32_t *addr, int32_t data);
    int32_t peekmem(std::map<uint8_t, std::vector<uint8_t>> &parameters); //int32_t peekmem(int32_t argcnt, int32_t *addr);
    uint32_t rnd(int32_t argcnt, uint32_t low, uint32_t high);
    uint32_t dice(int32_t argcnt, uint32_t ndice, uint32_t nsides, uint32_t bonus);
    uint32_t absv(int32_t argcnt, int32_t val);
    int32_t minv(int32_t argcnt, int32_t val1, int32_t val2);
    int32_t maxv(int32_t argcnt, int32_t val1, int32_t val2);
    void diagnose(int32_t argcnt, uint32_t dtype, uint32_t parm);
    uint32_t heapfree(void);

    // Event functions
    void notify(int32_t argcnt, uint32_t index, uint32_t message, int32_t event, int32_t parameter);
    void cancel(int32_t argcnt, uint32_t index, uint32_t message, int32_t event, int32_t parameter);
    void drain_event_queue(void);
    void post_event(int32_t argcnt, uint32_t owner, int32_t event, int32_t parameter);
    void send_event(int32_t argcnt, uint32_t owner, int32_t event, int32_t parameter);
    uint32_t peek_event(void);
    void dispatch_event(void);
    void flush_event_queue(int32_t argcnt, int32_t owner, int32_t event, int32_t parameter);
    void flush_input_events(void);

    // Interface functions
    void init_interface(void);
    void shutdown_interface(void);
    void set_mouse_pointer(int32_t argcnt, uint32_t table, uint32_t number, int32_t hot_X,
       int32_t hot_Y, uint32_t scale, uint32_t fade_table, uint32_t fade_level);
    void set_wait_pointer(int32_t argcnt, uint32_t number, int32_t hot_X, int32_t hot_Y);
    void standby_cursor(void);
    void resume_cursor(void);
    void show_mouse(void);
    void hide_mouse(void);
    uint32_t mouse_XY(void);
    uint32_t mouse_in_window(int32_t argcnt, uint32_t wnd);
    void lock_mouse(void);
    void unlock_mouse(void);
    void getkey(void);

    // Graphics-related functions
    void init_graphics(void);
    void draw_dot(int32_t argcnt, uint32_t page, uint32_t x, uint32_t y, uint32_t color);
    void draw_line(int32_t argcnt, uint32_t page,
       uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color);
    void line_to(int32_t argcnt, uint32_t x, uint32_t y, uint32_t color, ...);
    void draw_rectangle(int32_t argcnt, uint32_t wndnum, int32_t x1, int32_t y1, int32_t x2,
       int32_t y2, uint32_t color);
    void fill_rectangle(int32_t argcnt, uint32_t wndnum, int32_t x1, int32_t y1, int32_t x2,
       int32_t y2, uint32_t color);
    void hash_rectangle(int32_t argcnt, uint32_t wndnum, int32_t x1, int32_t y1, int32_t x2,
       int32_t y2, uint32_t color);
    uint32_t get_bitmap_height(int32_t argcnt, uint32_t table, uint32_t number);
    void draw_bitmap(int32_t argcnt, uint32_t page, uint32_t table, uint32_t number,
       int32_t x, int32_t y, uint32_t scale, uint32_t flip, uint32_t fade_table, uint32_t
       fade_level);
    uint32_t visible_bitmap_rect(int32_t argcnt, int32_t x, int32_t y,
    uint32_t flip, uint32_t table, uint32_t number, int16_t *array);
    void set_palette(int32_t argcnt, uint32_t region, uint32_t resource);
    void refresh_window(int32_t argcnt, uint32_t src, uint32_t target);
    void wipe_window(int32_t argcnt, uint32_t window, uint32_t color);
    void shutdown_graphics(void);
    void wait_vertical_retrace(void);
    uint32_t read_palette(int32_t argcnt, uint32_t regnum);
    void write_palette(int32_t argcnt, uint32_t regnum, uint32_t value);
    void pixel_fade(int32_t argcnt, uint32_t src_wnd, uint32_t dest_wnd, uint32_t intervals);
    void color_fade(int32_t argcnt, uint32_t src_wnd, uint32_t dest_wnd);
    void light_fade(int32_t argcnt, uint32_t src_wnd, uint32_t color);

    uint32_t assign_window(int32_t argcnt, uint32_t owner, uint32_t x1, uint32_t y1,
       uint32_t x2, uint32_t y2);
    uint32_t assign_subwindow(int32_t argcnt, uint32_t owner, uint32_t parent,
       uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);
    void release_window(int32_t argcnt, uint32_t window);
    uint32_t get_x1(int32_t argcnt, uint32_t window);
    uint32_t get_x2(int32_t argcnt, uint32_t window);
    uint32_t get_y1(int32_t argcnt, uint32_t window);
    uint32_t get_y2(int32_t argcnt, uint32_t window);
    void set_x1(int32_t argcnt, uint32_t window, uint32_t x1);
    void set_x2(int32_t argcnt, uint32_t window, uint32_t x2);
    void set_y1(int32_t argcnt, uint32_t window, uint32_t y1);
    void set_y2(int32_t argcnt, uint32_t window, uint32_t y2);

    void text_window(int32_t argcnt, uint32_t wndnum, uint32_t wnd);
    void text_style(int32_t argcnt, uint32_t wndnum, uint32_t font, uint32_t
       justify);
    void text_xy(int32_t argcnt, uint32_t wndnum, uint32_t htab, uint32_t vtab);
    void text_color(int32_t argcnt, uint32_t wndnum, uint32_t current, uint32_t new_color); // new_color was original new
    void text_refresh_window(int32_t argcnt, uint32_t wndnum, int32_t wnd);
    int32_t get_text_x(int32_t argcnt, uint32_t wndnum);
    int32_t get_text_y(int32_t argcnt, uint32_t wndnum);
    void home(int32_t argcnt, uint32_t wndnum);

    void print(int32_t argcnt, uint32_t wndnum, uint32_t format, ...);
    void sprint(int32_t argcnt, uint32_t wndnum, int8_t *format, ...);
    void dprint(int32_t argcnt, int8_t *format, ...);
    void aprint(int32_t argcnt, int8_t *format, ...);
    void crout(int32_t argcnt, uint32_t wndnum);
    uint32_t char_width(int32_t argcnt, uint32_t wndnum, uint32_t ch);
    uint32_t font_height(int32_t argcnt, uint32_t wndnum);

    void solid_bar_graph(int32_t argcnt, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
       uint32_t lb_border, uint32_t tr_border, uint32_t bkgnd, uint32_t grn, uint32_t yel,
       uint32_t red, int32_t val, int32_t min, int32_t crit, int32_t max);

    // Sound-related functions
    void init_sound(int32_t argcnt, uint32_t errprompt);
    void shutdown_sound(void);
    void load_sound_block(int32_t argcnt, uint32_t first_block, uint32_t last_block, uint32_t *array);
    void sound_effect(int32_t argcnt, uint32_t index);
    void play_sequence(int32_t argcnt, uint32_t LA_version, uint32_t AD_version, uint32_t PC_version);
    void load_music(void);
    void unload_music(void);
    void set_sound_status(int32_t argcnt, uint32_t status);

    // Eye III object management
    int32_t create_object(int32_t argcnt, uint32_t name);
    //int32_t create_program(int32_t argcnt, int32_t index, uint32_t name);
    int32_t destroy_object(int32_t argcnt, int32_t index);
    void thrash_cache(void);
    uint32_t flush_cache(int32_t argcnt, uint32_t goal);

    // Eye III support functions
    int32_t step_X(int32_t argcnt, uint32_t x, uint32_t fdir, uint32_t mtype, uint32_t distance);
    int32_t step_Y(int32_t argcnt, uint32_t y, uint32_t fdir, uint32_t mtype, uint32_t distance);
    uint32_t step_FDIR(int32_t argcnt, uint32_t fdir, uint32_t mtype);

    int32_t step_square_X(int32_t argcnt, uint32_t x, uint32_t r, uint32_t dir);
    int32_t step_square_Y(int32_t argcnt, uint32_t y, uint32_t r, uint32_t dir);
    int32_t step_region(int32_t argcnt, uint32_t r, uint32_t dir);

    uint32_t distance(int32_t argcnt, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2);
    uint32_t seek_direction(int32_t argcnt, uint32_t obj_x, uint32_t obj_y, uint32_t dest_x, uint32_t dest_y);

    uint32_t spell_request(int32_t argcnt, int8_t *stat, int8_t *cnt, uint32_t typ, uint32_t num);
    uint32_t spell_list(int32_t argcnt, int8_t *cnt, uint32_t typ, uint32_t lvl, int8_t *list, uint32_t max);
    void magic_field(int32_t argcnt, uint32_t p, uint32_t redfield, uint32_t yelfield, int32_t sparkle);
    void do_dots(int32_t argcnt, int32_t view, int32_t scrn, int32_t exp_x, int32_t exp_y,
       int32_t scale, int32_t power, int32_t dots, int32_t life, int32_t upval, int8_t *colors);
    void do_ice(int32_t argcnt, int32_t view, int32_t scrn, int32_t dots, int32_t mag, int32_t grav, int32_t life, int32_t colors);

    void read_save_directory(void);
    int8_t *savegame_title(int32_t argcnt, uint32_t num);
    void write_save_directory(void);

    uint32_t save_game(int32_t argcnt, uint32_t slotnum, uint32_t lvlnum);
    void suspend_game(int32_t argcnt, uint32_t cur_lvl);
    void resume_items(int32_t argcnt, uint32_t first, uint32_t last, uint32_t restoring);
    void resume_level(int32_t argcnt, uint32_t cur_lvl);
    void change_level(int32_t argcnt, uint32_t old_lvl, uint32_t new_lvl);
    void restore_items(int32_t argcnt, uint32_t slotnum);
    void restore_level_objects(int32_t argcnt, uint32_t slotnum, uint32_t lvlnum);
    void read_initial_items(void);
    void write_initial_tempfiles(void);
    void create_initial_binary_files(void);
    void launch(std::map<uint8_t, std::vector<uint8_t>> parameters); // void launch(int32_t argcnt, int8_t *dirname, int8_t *prgname, int8_t *argn1, int8_t *argn2);

    // Eye II savegame file access
    void *open_transfer_file(int32_t argcnt, int8_t *filename);
    void close_transfer_file(void);
    int32_t player_attrib(int32_t argcnt, uint32_t plrnum, uint32_t offset, uint32_t size);
    int32_t item_attrib(int32_t argcnt, uint32_t plrnum, uint32_t invslot, uint32_t attrib);
    int32_t arrow_count(int32_t argcnt, uint32_t plrnum);
};

}

#endif /* FUNCTION_HPP */
