/*
 * dmod in a browser - the page as dmodOS shows it, without dmodOS.
 *
 * On dmodOS the page is a view shown by carhmi: dmod.prop / get / set /
 * onchange / emit are the channel between them (dmview's properties and
 * events), compiled into the view. The conversion does not run this file:
 * here it plays the part of carhmi - a library of LIBRARY tracks, a window
 * of ROWS of them in the queue, a second at a time - so the page works in a
 * browser as it does on the device.
 */
(function () {
    const ROWS = 20;
    const LIBRARY = 40;
    const props = {};
    const listeners = {};
    const library = [];
    let first = 0;                      /* The queue's window */
    let track = 0;                      /* Playing, of the library */

    function changed(name) {
        (listeners[name] || []).forEach(fn => fn());
        /* data-dmod-width="name": as wide as the property says (data-dmod-scale: 1000) */
        document.querySelectorAll('[data-dmod-width="' + name + '"]').forEach(e => {
            const scale = parseInt(e.dataset.dmodScale || '1000', 10);
            e.style.width = Math.max(0, Math.min(scale, props[name] || 0)) * 100 / scale + '%';
        });
    }

    /* What carhmi does: it sets the properties, the view's handlers show them */
    function app(name, value) {
        props[name] = value;
        changed(name);
    }

    function time(seconds) {
        return Math.floor(seconds / 60) + ':' + String(seconds % 60).padStart(2, '0');
    }

    function row(t) {
        return (t >= first && t < first + ROWS) ? t - first : -1;
    }

    /* The queue's window at `at`: its rows, then where it is (the view scrolls by it) */
    function window_at(at) {
        first = Math.max(0, Math.min(at, Math.max(0, library.length - ROWS)));
        let count = 0;
        for (let i = 0; i < ROWS; i++) {
            const t = library[first + i];
            app('row' + i + '.title', t ? t[0] : '');
            app('row' + i + '.artist', t ? t[1] : '');
            app('row' + i + '.time', t ? time(t[2]) : '');
            count += t ? 1 : 0;
        }
        app('count', count);
        app('track', row(track));
        app('first', first);
    }

    function play(t) {
        const n = library.length;
        track = ((t % n) + n) % n;
        const d = library[track];
        app('title', d[0]);
        app('artist', d[1]);
        app('duration', d[2]);
        app('position', 0);
        app('progress', 0);
        app('playing', 1);
        if (row(track) < 0)
            window_at(track < first ? track : track - ROWS + 1);
        else
            app('track', row(track));
    }

    window.addEventListener('DOMContentLoaded', () => {
        /* The properties of data-dmod-width elements, from data-dmod-value */
        document.querySelectorAll('[data-dmod-width]').forEach(e => {
            app(e.dataset.dmodWidth, parseInt(e.dataset.dmodValue || '0', 10));
        });
        /* The library: the design's tracks first, then numbered ones */
        for (let i = 0; i < ROWS && props['row' + i + '.title']; i++) {
            const t = props['row' + i + '.time'].split(':');
            library.push([props['row' + i + '.title'], props['row' + i + '.artist'],
                          parseInt(t[0], 10) * 60 + parseInt(t[1], 10)]);
        }
        for (let i = library.length; i < LIBRARY; i++)
            library.push(['Utwór ' + (i + 1), 'Wykonawca ' + (i % 7 + 1), 150 + (i * 37) % 150]);
        app('total', library.length);
        window_at(0);
        play(0);
        clock();
    });

    window.dmod = {
        prop(name, initial) {
            if (!(name in props))
                props[name] = initial;
            return props[name];
        },
        get(name) {
            return props[name];
        },
        set(name, value) {
            props[name] = value;
        },
        onchange(name, fn) {
            (listeners[name] = listeners[name] || []).push(fn);
        },
        emit(name, value) {
            if (name === 'toggle')
                app('playing', props['playing'] ? 0 : 1);
            else if (name === 'next')
                play(track + 1);
            else if (name === 'prev')
                play(track - 1);
            else if (name === 'select')
                play(first + value);
            else if (name === 'scroll')
                window_at(first + value);       /* The list at an end: more of it */
        },
    };

    /* The clock: carhmi's is the RTC's, here the browser's */
    function clock() {
        const now = new Date();
        app('clock', String(now.getHours()).padStart(2, '0') + ':' + String(now.getMinutes()).padStart(2, '0'));
    }

    setInterval(() => {
        clock();
        if (!props['playing'])
            return;
        if (props['position'] + 1 >= props['duration'])
            play(track + 1);
        else
        {
            app('position', props['position'] + 1);
            app('progress', Math.floor(props['position'] * 1000 / props['duration']));
        }
    }, 1000);
})();
