/* Browser counterpart of letters.c. The device uses the C controller and
 * the compiled property/event bindings in car_hmi.html. */
(function () {
    'use strict';
    const data = window.literkiData, words = data.words, alphabet = data.alphabet;
    const dmod = window.dmod;
    const state = { active:false, solved:false, wrong:false, round:0, score:0, mode:0,
        word:0, filled:0, options:[], order:[], used:[], placed:[], deadline:0 };
    let seed = (Date.now() ^ 0x4c495445) >>> 0 || 1, deck = [], modes = [], lastWord = -1, lastMode = -1;
    let opened = false, resumeMusic = false, revision = 0;
    let speechSerial = 0, speechBusy = false, audio = null, rewardContext = null;
    function random() { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return seed >>> 0; }
    function shuffle(values) {
        for (let i = values.length; i > 1; i--) { const j = random() % i; [values[i - 1], values[j]] = [values[j], values[i - 1]]; }
        return values;
    }
    function bag(count, last) {
        const values = shuffle(Array.from({length:count}, (_, i) => i));
        if (values[0] === last) [values[0], values[1]] = [values[1], values[0]];
        return values;
    }
    function publish() {
        const word = words[state.word], letters = Array.from(word.word.toLocaleUpperCase('pl'));
        const p = (key, value) => dmod.publish('game.' + key, value);
        p('active', state.active ? 1 : 0); p('mode', state.mode); p('solved', state.solved ? 1 : 0);
        p('wrong', state.wrong ? 1 : 0); p('round', state.round); p('length', letters.length);
        p('title', ['Pierwsza literka','Znajdź obrazek','Ułóż słowo','Czytamy!'][state.mode]);
        p('score', 'Gwiazdki: ' + state.score);
        p('target', state.mode === 3 ? word.word.toLocaleUpperCase('pl') : word.letter);
        p('status', state.solved ? 'Brawo! Świetnie!' : state.wrong ? 'Spróbuj jeszcze raz' : [
            'Jaką literką zaczyna się to słowo?', 'Co zaczyna się na tę literkę?',
            'Dotykaj literek po kolei', 'Przeczytaj i wybierz obrazek'][state.mode]);
        p('object', 'lit_' + word.id); p('spell_image', 'lit_' + word.id);
        p('assembled', letters.map((letter, i) => i < state.filled ? letter : '_').join(' '));
        for (let i = 0; i < 3; i++) {
            p('letter' + i, state.mode === 0 ? alphabet[state.options[i]] : '');
            p('picture' + i, 'lit_' + words[state.mode === 0 ? 0 : state.options[i]].id);
        }
        for (let i = 0; i < 16; i++) p('tile' + i, i < letters.length && !state.used[i] ? letters[state.order[i]] : '');
        p('revision', ++revision);
    }
    function cancel() {
        speechSerial++; speechBusy = false;
        if (window.speechSynthesis) window.speechSynthesis.cancel();
        if (audio) { audio.pause(); audio = null; }
        if (rewardContext) { rewardContext.close().catch(() => {}); rewardContext = null; }
    }
    function say(file, text, serial, done) {
        if (serial !== speechSerial) return;
        speechBusy = true;
        let finished = false;
        const finish = () => { if (finished) return; finished = true; if (serial === speechSerial) { speechBusy = false; if (done) done(); } };
        const fallback = () => {
            if (serial !== speechSerial) return;
            if (!window.speechSynthesis || !window.SpeechSynthesisUtterance) { finish(); return; }
            const utterance = new SpeechSynthesisUtterance(text);
            utterance.lang = 'pl-PL'; utterance.rate = 0.86;
            const voices = window.speechSynthesis.getVoices().filter(v => /^pl/i.test(v.lang));
            utterance.voice = voices.find(v => /Paulina|Zofia|Agnieszka|Maja|Ewa|Zuzanna/i.test(v.name)) || voices[0] || null;
            utterance.onend = finish; utterance.onerror = finish;
            window.speechSynthesis.speak(utterance);
        };
        if ((data.audioFiles || []).includes(file)) {
            audio = new Audio('games/literki/audio/' + file);
            audio.onended = finish; audio.onerror = fallback;
            audio.play().catch(fallback);
        } else fallback();
        // Some platforms expose speech synthesis without an installed voice.
        setTimeout(finish, 12000);
    }
    function prompt() {
        cancel();
        const word = words[state.word];
        const initial = alphabet.indexOf(word.letter);
        const file = state.mode === 3 ? 'read.mp3' : state.mode === 1 ? 'letter_' + String(initial).padStart(2, '0') + '.mp3' :
            (state.mode === 0 ? 'first_' : 'spell_') + word.id + '.mp3';
        const text = state.mode === 3 ? 'Przeczytaj wyraz i wybierz pasujący obrazek.' : state.mode === 1 ?
            'Wybierz obrazek na literę ' + word.letter + '.' : state.mode === 0 ?
            'Jaką literką zaczyna się słowo: ' + word.word + '?' : 'Ułóż z literek słowo: ' + word.word + '.';
        say(file, text, speechSerial);
    }
    function reward() {
        cancel(); const serial = speechSerial; speechBusy = true;
        try {
            const Context = window.AudioContext || window.webkitAudioContext;
            const ctx = rewardContext = new Context();
            const start = ctx.currentTime;
            [523.25,659.25,783.99,1046.5].forEach((hz, i) => {
                const oscillator = ctx.createOscillator(), gain = ctx.createGain();
                oscillator.frequency.value = hz; oscillator.type = 'sine';
                gain.gain.setValueAtTime(0.2, start + i * .16);
                gain.gain.exponentialRampToValueAtTime(.001, start + i * .16 + .24);
                oscillator.connect(gain); gain.connect(ctx.destination);
                oscillator.start(start + i * .16); oscillator.stop(start + i * .16 + .25);
            });
            for (let i = 0; i < 18; i++) {
                const buffer = ctx.createBuffer(1, Math.floor(ctx.sampleRate * .055), ctx.sampleRate);
                const samples = buffer.getChannelData(0);
                for (let j = 0; j < samples.length; j++) samples[j] = (Math.random()*2-1) * (1-j/samples.length) * .27;
                const clap = ctx.createBufferSource(); clap.buffer = buffer; clap.connect(ctx.destination);
                clap.start(start + .4 + i*.067 + Math.random()*.025);
            }
            ctx.resume().catch(() => {});
        } catch (_) { /* Visual reward still works when audio is unavailable. */ }
        setTimeout(() => {
            if (serial !== speechSerial) return;
            if (rewardContext) { rewardContext.close().catch(() => {}); rewardContext = null; }
            say('bravo.mp3', 'Brawo! Świetnie ci poszło!', serial);
        }, 1800);
    }
    function next() {
        if (!deck.length) deck = bag(words.length, lastWord);
        if (!modes.length) modes = bag(4, lastMode);
        state.word = lastWord = deck.shift(); state.mode = lastMode = modes.shift();
        state.round = state.round % 0x07fffffe + 1;
        state.solved = state.wrong = false; state.filled = 0; state.used = []; state.placed = [];
        const chars = Array.from(words[state.word].word.toLocaleUpperCase('pl'));
        state.order = shuffle(chars.map((_,i) => i));
        if (state.order.every((index,i) => chars[index] === chars[i])) {
            const different = state.order.findIndex(index => chars[index] !== chars[state.order[0]]);
            if (different > 0) [state.order[0],state.order[different]] = [state.order[different],state.order[0]];
        }
        state.correct = random() % 3; state.options = [];
        const right = state.mode === 0 ? alphabet.indexOf(words[state.word].letter) : state.word;
        for (let i = 0; i < 3; i++) {
            let value = right;
            if (i !== state.correct) do {
                value = random() % (state.mode === 0 ? alphabet.length : words.length);
            } while (state.options.includes(value) || (state.mode === 0 ? value === right : words[value].letter === words[state.word].letter));
            state.options.push(value);
        }
        publish(); prompt();
    }
    function answer(round, choice) {
        if (!state.active || state.solved || state.round !== round) return;
        let correct = false;
        if (state.mode === 2) {
            const chars = Array.from(words[state.word].word.toLocaleUpperCase('pl'));
            if (choice >= chars.length || state.used[choice]) return;
            correct = chars[state.order[choice]] === chars[state.filled];
            if (correct) { state.used[choice] = true; state.placed.push(choice); state.filled++; }
            if (correct && state.filled < chars.length) { state.wrong = false; publish(); return; }
        } else { if (choice < 0 || choice >= 3) return; correct = choice === state.correct; }
        state.wrong = !correct;
        if (correct) { state.solved = true; state.score++; state.deadline = performance.now() + 2100; reward(); }
        else { state.deadline = performance.now() + 1100; cancel(); say('again.mp3', 'Spróbuj jeszcze raz. Dasz radę!', speechSerial); }
        publish();
    }
    const previousEmit = dmod.emit.bind(dmod);
    dmod.emit = function(name, value) {
        if (!name.startsWith('game.')) return previousEmit(name, value);
        if (name === 'game.enter') {
            if (!opened) { opened = true; resumeMusic = !!dmod.get('playing'); dmod.publish('playing', 0); cancel(); say('welcome.mp3','Cześć! Pobawmy się literkami.',speechSerial); }
        } else if (name === 'game.leave') {
            if (opened) { opened = false; state.active = false; cancel(); if (resumeMusic) dmod.publish('playing',1); publish(); }
        } else if (name === 'game.start') { opened = true; state.active = true; state.score = 0; next(); }
        else if (name === 'game.back') { state.active = false; cancel(); publish(); }
        else if (name === 'game.repeat' && state.active && !state.solved) prompt();
        else if (name === 'game.answer') answer(Math.floor(value/16),value%16);
        else if (name === 'game.undo' && state.active && !state.solved && state.mode === 2 && state.filled) {
            state.used[state.placed.pop()] = false; state.filled--; state.wrong = false; publish();
        }
    };
    setInterval(() => {
        if (!state.active || performance.now() < state.deadline) return;
        if (state.solved) { if (!speechBusy) next(); }
        else if (state.wrong) { state.wrong = false; publish(); }
    }, 80);
    window.addEventListener('pagehide', cancel);
    // Inspectable browser model for automated input tests and previews.
    window.literkiGame = { state, words, answer };
})();
