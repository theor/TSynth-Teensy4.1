const fs = require('fs');
try {
    fs.readdir(__dirname, (err, files) => {
        var csv = 'name,osc level a,osc level b," pinknoise level "," white noise level"," unison mode"," osc fx"," detune"," lfo sync freq"," lfo tempo value"," KeytrackingAmount"," glideSpeed",oscPitchA,oscPitchB,getWaveformA,getWaveformB,getPwmSource,getPwmAmtA,getPwmAmtB,getPwmRate,getPwA,getPwB,getResonance,getCutoff,getFilterMixer,getFilterEnvelope,getPitchLfoAmount,getPitchLfoRate,getPitchLfoWaveform,getPitchLfoRetrig,getPitchLfoMidiClockSync,getFilterLfoRate,getFilterLfoRetrig,getFilterLfoMidiClockSync,getFilterLfoAmt,getFilterLfoWaveform,getFilterAttack,getFilterDecay,getFilterSustain,getFilterRelease,getAmpAttack,getAmpDecay,getAmpSustain,getAmpRelease,getEffectAmount,getEffectMix,getPitchEnvelope,velocitySens,chordDetune,getMonophonicMode,0,0\n';
        console.log(files);
        files = files.filter(x => x.indexOf('.') == -1).sort((a, b) => Number(a) - Number(b));
        files.forEach(file => {
            const preset = fs.readFileSync(file);
            csv += preset.toString().trimEnd() + "\n";
        });
        console.log(csv);
        fs.writeFileSync("presets.csv", csv);
    });

} catch (error) {
    console.log(error);

}
