/*
 * dmod in a browser - the page as dmodOS shows it, without dmodOS.
 *
 * On dmodOS the page is a view shown by carhmi: dmod.prop / get / set /
 * onchange / emit are the channel between them (dmview's properties and
 * events), compiled into the view. The conversion does not run this file:
 * here it plays the part of carhmi - a library of LIBRARY tracks, a window
 * of ROWS of them in the queue, a second at a time, a volume - so the page
 * works in a browser as it does on the device.
 */
(function () {
    const ROWS = 20;
    const LEAD = 10;                    /* Rows before a track the window is moved to */
    const LIBRARY = 40;
    const props = {};
    const listeners = {};
    const library = [];
    let fuelRemaining = 900000;
    let fuelUpdated = performance.now();
    let fuelVisible = false;
    let fuelStage = 0;
    let engineOn = true;
    let fuelNotice = 0;
    let fuelWarningLeft = 0;
    let pumpAudio = null;

    // Local Web Audio pump; created only in response to a user gesture.
    function pumpSound(on) {
        if (!on) {
            if (pumpAudio) {
                const sound = pumpAudio;
                pumpAudio = null;
                sound.gain.gain.setTargetAtTime(0, sound.context.currentTime, 0.025);
                setTimeout(() => sound.context.close(), 160);
            }
            return;
        }
        if (pumpAudio) return;
        const Audio = window.AudioContext || window.webkitAudioContext;
        if (!Audio) return;
        try {
            const context = new Audio();
            const gain = context.createGain();
            gain.gain.value = 0.12;
            gain.connect(context.destination);
            const motor = context.createOscillator();
            motor.type = 'triangle'; motor.frequency.value = 105;
            motor.connect(gain); motor.start();
            const buffer = context.createBuffer(1, context.sampleRate, context.sampleRate);
            const samples = buffer.getChannelData(0);
            for (let i = 0; i < samples.length; i++) samples[i] = Math.random() * 2 - 1;
            const flow = context.createBufferSource();
            flow.buffer = buffer; flow.loop = true;
            const filter = context.createBiquadFilter();
            filter.type = 'lowpass'; filter.frequency.value = 700;
            flow.connect(filter); filter.connect(gain); flow.start();
            pumpAudio = { context, gain };
            context.resume().catch(() => {});
        } catch (_) { /* Refuelling stays usable without an audio device. */ }
    }

    function fuelUpdate() {
        const now = performance.now();
        const elapsed = Math.max(0, now - fuelUpdated);
        fuelUpdated = now;
        fuelWarningLeft = Math.max(0, fuelWarningLeft - elapsed);
        if (fuelVisible && fuelStage === 2) {
            fuelRemaining = Math.min(900000, fuelRemaining + elapsed * 45);
            if (fuelRemaining >= 900000) { fuelStage = 3; pumpSound(false); }
        } else if (engineOn) {
            fuelRemaining = Math.max(0, fuelRemaining - elapsed);
        }
        if (fuelRemaining <= 0 && engineOn) { engineOn = false; fuelNotice = 1; }
        if (fuelRemaining > 0 && fuelNotice === 1) fuelNotice = 0;
        app('engine', engineOn ? 1 : 0);
        app('fuel.notice', fuelNotice);
        app('fuel.warning', fuelVisible && fuelNotice === 2 && fuelWarningLeft > 0 && Math.floor((2400 - fuelWarningLeft) / 300) % 2 === 0 ? 1 : 0);
        app('fuel.level', Math.floor(fuelRemaining * 1000 / 900000));
        app('fuel.stage', fuelStage);
    }
    setInterval(fuelUpdate, 100);
    window.addEventListener('pagehide', () => pumpSound(false));

    let first = 0;                      /* The queue's window */
    let track = 0;                      /* Playing, of the library */

    function changed(name) {
        if (typeof props[name] === 'string' && props[name].startsWith('lit_')) {
            document.querySelectorAll('[data-dmod-src="' + name + '"]').forEach(image => {
                image.src = 'games/literki/images/' + props[name].slice(4) + '.png';
            });
        }
        (listeners[name] || []).forEach(fn => fn());
        if (name === 'fuel.level') {
            const liquid = document.getElementById('fuel-liquid');
            if (liquid) {
                const height = Math.max(0, Math.min(1000, props[name] || 0)) * 118 / 1000;
                liquid.style.height = height + 'px';
                liquid.style.top = (118 - height) + 'px';
            }
        }
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
            window_at(track - LEAD);
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
        publish: app,
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
            if (name === 'fuel.open' || name === 'fuel.close' || name === 'fuel.filler' || name === 'engine') {
                fuelUpdate();
                if (name === 'engine') {
                    if (engineOn) { engineOn = false; fuelNotice = 0; fuelWarningLeft = 0; }
                    else if (fuelRemaining <= 0) fuelNotice = 1;
                    else if (fuelVisible && (fuelStage === 2 || fuelStage === 3)) fuelNotice = 3;
                    else { engineOn = true; fuelNotice = 0; }
                }
                else if (name === 'fuel.open') { fuelVisible = true; fuelStage = 0; fuelWarningLeft = 0; }
                else if (name === 'fuel.close') { fuelVisible = false; fuelStage = 0; fuelWarningLeft = 0; if (fuelNotice !== 1) fuelNotice = 0; }
                else if (fuelVisible) {
                    if (engineOn && (fuelStage === 0 || fuelStage === 1)) { fuelNotice = 2; fuelWarningLeft = 2400; }
                    else {
                        if (fuelNotice !== 1) fuelNotice = 0;
                        if (fuelStage === 0) fuelStage = 1;
                        else if (fuelStage === 1) fuelStage = fuelRemaining < 900000 ? 2 : 3;
                        else if (fuelStage === 4) fuelStage = 0;
                        else fuelStage = 4;
                    }
                }
                pumpSound(fuelVisible && fuelStage === 2);
                fuelUpdate();
            }
            else if (name === 'toggle')
                app('playing', props['playing'] ? 0 : 1);
            else if (name === 'next')
                play(track + 1);
            else if (name === 'prev')
                play(track - 1);
            else if (name === 'select')
                play(first + value);
            else if (name === 'scroll')
                window_at(first + value);       /* The list at an end: more of it */
            else if (name === 'jump')
                window_at(value - LEAD);        /* The position bar dragged: that track, at the top */
            else if (name === 'volume') {       /* A new volume turns the sound on, too */
                app('volume', Math.max(0, Math.min(100, value)));
                app('muted', 0);
            }
            else if (name === 'mute')
                app('muted', props['muted'] ? 0 : 1);
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

    // Browser counterpart of assets/navigation-gestures.dmvs. The converter
    // intentionally excludes this file; the device uses captured DMV contacts.
    window.addEventListener('DOMContentLoaded', () => {
        const view = document.getElementById('view-nav');
        const near = document.getElementById('nav-map-near');
        const wide = document.getElementById('nav-map-wide');
        const car = document.getElementById('nav-car');
        const fingers = new Map();
        let separation = 0;
        const scale = () => view.getBoundingClientRect().width / 430;
        const point = e => { const r = view.getBoundingClientRect(); return { x:(e.clientX-r.left)/scale(), y:(e.clientY-r.top)/scale() }; };
        const position = e => { const a=e.getBoundingClientRect(), b=view.getBoundingClientRect(); return {x:(a.left-b.left)/scale(),y:(a.top-b.top)/scale()}; };
        const active = () => getComputedStyle(near).opacity === '0' ? wide : near;
        function place(e, x, y) {
            e.style.transition = 'none';
            e.style.transform = 'translate(' + (x - parseFloat(getComputedStyle(e).left)) + 'px, ' + (y - parseFloat(getComputedStyle(e).top)) + 'px)';
        }
        function manual() {
            navRunning = 0;
            navPause.innerText = 'Wznów';
            navStatus.innerText = 'Przegląd mapy';
        }
        function move(dx, dy) {
            const a=active(), p=position(a);
            dx=Math.max(430-a.clientWidth,Math.min(0,p.x+dx))-p.x;
            dy=Math.max(248-a.clientHeight,Math.min(0,p.y+dy))-p.y;
            [near,wide].forEach(e=>{const p=position(e);place(e,p.x+dx,p.y+dy);});
            car.style.left=(parseFloat(getComputedStyle(car).left)+dx)+'px';
            car.style.top=(parseFloat(getComputedStyle(car).top)+dy)+'px';
        }
        function zoom(close, cx, cy) {
            const from=active(), to=close?near:wide;
            if(from===to)return;
            manual();
            const factor=close?1.25:0.8, p=position(from);
            place(to,cx+(p.x-cx)*factor,cy+(p.y-cy)*factor);
            car.style.left=(cx+(parseFloat(getComputedStyle(car).left)+14-cx)*factor-14)+'px';
            car.style.top=(cy+(parseFloat(getComputedStyle(car).top)+18-cy)*factor-18)+'px';
            near.style.opacity=close?'1':'0';wide.style.opacity=close?'0':'1';
            move(0,0);
        }
        function recenter() {
            car.style.left='176px';car.style.top='160px';
            near.style.transition='';wide.style.transition='';
            navUpdate();
        }
        [['nav-zoom-in',true],['nav-zoom-out',false]].forEach(([id,close])=>document.getElementById(id).addEventListener('click',e=>{
            e.stopImmediatePropagation();zoom(close,190,178);
        },true));
        document.getElementById('nav-recenter').addEventListener('click',recenter,true);
        navPause.addEventListener('click',()=>{if(!navRunning)recenter();},true);
        view.addEventListener('pointerdown',e=>{
            const p=point(e);
            if(e.target.closest('button')||p.x>=380||p.y<76||p.y>=194)return;
            e.preventDefault();view.setPointerCapture(e.pointerId);fingers.set(e.pointerId,p);
            if(fingers.size===2){const [a,b]=[...fingers.values()];separation=Math.hypot(a.x-b.x,a.y-b.y);}
        });
        view.addEventListener('pointermove',e=>{
            if(!fingers.has(e.pointerId))return;
            e.preventDefault();const previous=fingers.get(e.pointerId),p=point(e);fingers.set(e.pointerId,p);
            if(fingers.size===1){manual();move(p.x-previous.x,p.y-previous.y);}
            else if(fingers.size===2&&separation>0){
                const [a,b]=[...fingers.values()], ratio=Math.hypot(a.x-b.x,a.y-b.y)/separation;
                if(ratio>=1.15||ratio<=0.85){zoom(ratio>=1.15,(a.x+b.x)/2,(a.y+b.y)/2);}
            }
        });
        function release(e){fingers.delete(e.pointerId);if(fingers.size===0){separation=0;}}
        view.addEventListener('pointerup',release);view.addEventListener('pointercancel',release);
    });
})();
