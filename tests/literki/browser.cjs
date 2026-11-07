/* Run with NODE_PATH pointing to a Playwright installation, with assets/ served
 * at CARHMI_URL (default http://127.0.0.1:8765/car_hmi.html). */
const {chromium} = require('playwright');
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
(async () => {
    const output = process.env.CARHMI_SCREENSHOTS || '/tmp/carhmi-games-browser';
    fs.mkdirSync(output,{recursive:true});
    const browser = await chromium.launch({headless:true});
    for (const width of [480,320]) {
        const page = await browser.newPage({viewport:{width:width===480?900:320,height:600}});
        const errors=[];
        page.on('pageerror',e=>errors.push(e.message));
        await page.addInitScript(() => {
            window.lessonSpeech=[];
            if(window.speechSynthesis) window.speechSynthesis.speak = utterance => {
                window.lessonSpeech.push(utterance.text);
                queueMicrotask(()=>{if(utterance.onend)utterance.onend();});
            };
        });
        const press = async selector => {
            const button=page.locator(selector);
            await button.hover();
            const appearance=()=>button.evaluate(node=>{
                const s=getComputedStyle(node);
                return [s.backgroundColor,s.borderColor,s.color,s.opacity,s.transform];
            });
            const before=await appearance();
            await page.mouse.down();
            assert.notDeepEqual(await appearance(),before,`${selector} reacts on touch down`);
            await page.mouse.up();
        };
        await page.goto(process.env.CARHMI_URL || 'http://127.0.0.1:8765/car_hmi.html',{waitUntil:'networkidle'});
        await page.addStyleTag({content:'#hmi-preview {transform:none!important;padding:0!important;border:0!important}'});
        await press('#games-nav');
        await page.waitForTimeout(350);
        assert.equal(await page.evaluate(()=>dmod.get('playing')),0);
        await page.locator('#hmi-screen').screenshot({path:path.join(output,`menu-${width}.png`)});
        await press('#literki-start');
        const kinds=new Set();
        const snapshot=()=>page.evaluate(()=>JSON.parse(JSON.stringify(window.literkiGame.state)));
        for(let roundIndex=0;roundIndex<4;roundIndex++) {
            let state=await snapshot();
            kinds.add(state.mode);
            const originalRound=state.round, originalScore=state.score;
            await press('#lesson-repeat');
            if(state.mode===3) {
                const speech=await page.evaluate(()=>window.lessonSpeech.at(-1));
                assert.equal(speech,'Przeczytaj wyraz i wybierz pasujący obrazek.');
            }
            await page.waitForTimeout(70);
            const images=await page.locator('#game-play img').evaluateAll(nodes=>nodes.filter(n=>{
                const style=getComputedStyle(n);return n.getBoundingClientRect().width&&style.display!=='none';
            }).map(n=>({complete:n.complete,width:n.naturalWidth})));
            assert(images.every(i=>i.complete&&i.width>0),'all picture sources load');
            await page.locator('#hmi-screen').screenshot({path:path.join(output,`mode${state.mode}-${width}.png`)});
            if(state.mode===2) {
                let chars=await page.evaluate(()=>Array.from(literkiGame.words[literkiGame.state.word].word.toLocaleUpperCase('pl')));
                const first=state.order.findIndex(v=>chars[v]===chars[0]);
                await press('#lesson-tile'+first);
                assert.equal((await snapshot()).filled,1);
                await press('#lesson-tile'+first);
                assert.equal((await snapshot()).filled,1,'same tile cannot be used twice');
                await press('#lesson-undo');
                assert.equal((await snapshot()).filled,0);
                while(!(state=await snapshot()).solved) {
                    const slot=state.order.findIndex((v,i)=>!state.used[i]&&chars[v]===chars[state.filled]);
                    assert(slot>=0);
                    await press('#lesson-tile'+slot);
                }
            } else {
                const prefix=state.mode===0?'#lesson-letter':'#lesson-picture';
                await press(prefix+((state.correct+1)%3));
                const wrong=await snapshot();assert(wrong.wrong&&!wrong.solved);assert.equal(wrong.score,originalScore);
                await press(prefix+state.correct);
            }
            state=await snapshot();assert(state.solved);assert.equal(state.score,originalScore+1);
            await page.locator('#hmi-screen').screenshot({path:path.join(output,`reward-${width}.png`)});
            await page.waitForFunction(previous=>window.literkiGame.state.round!==previous,originalRound,{timeout:16000});
        }
        assert.equal(kinds.size,4);
        await press('#lesson-back');
        assert(!(await snapshot()).active);
        await press('[data-target="home"]');
        assert.equal(await page.evaluate(()=>dmod.get('playing')),1,'music resumes on leaving Games');
        assert.deepEqual(errors,[]);
        console.log(`PASS browser ${width}: pressed feedback, real clicks, all four modes, wrong answers, undo, duplicate tiles, narration, page exit`);
        await page.close();
    }
    await browser.close();
})().catch(error=>{console.error(error);process.exit(1);});
