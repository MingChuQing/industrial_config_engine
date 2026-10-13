let runtimePromise;
async function initialize(){
  self.postMessage({type:'progress',stage:'loading'});
  const runtimeURL=new URL('./pyodide/',import.meta.url);
  const bundlePromise=fetch(new URL('./bundle.json',import.meta.url)).then(r=>{if(!r.ok)throw new Error('Cannot load saved simulator data');return r.json()});
  const {loadPyodide}=await import(new URL('pyodide.mjs',runtimeURL));
  const pyodide=await loadPyodide({indexURL:runtimeURL.href});
  const bundle=await bundlePromise;
  for(const [path,text] of Object.entries(bundle.files)){
    const target='/engine/'+path;
    pyodide.FS.mkdirTree(target.slice(0,target.lastIndexOf('/')));
    pyodide.FS.writeFile(target,text,{encoding:'utf8'});
  }
  const bridge=await fetch(new URL('./browser_demo.py',import.meta.url)).then(r=>{if(!r.ok)throw new Error('Cannot load demo entry point');return r.text()});
  pyodide.FS.writeFile('/engine/scripts/browser_demo.py',bridge,{encoding:'utf8'});
  pyodide.runPython('import sys\nsys.path.insert(0,"/engine/scripts")\nfrom browser_demo import run_demo');
  return {pyodide,bundle};
}
self.onmessage=async event=>{
  const {id,recipe,scenario}=event.data;
  try{
    if(!runtimePromise)runtimePromise=initialize().catch(error=>{runtimePromise=undefined;throw error});
    const {pyodide,bundle}=await runtimePromise;
    self.postMessage({type:'progress',stage:'running',scenario});
    pyodide.globals.set('demo_recipe',recipe);
    pyodide.globals.set('demo_scenario',scenario);
    const json=await pyodide.runPythonAsync('run_demo(demo_recipe,demo_scenario)');
    const result=JSON.parse(json);
    result.source_commit=bundle.source_commit;
    self.postMessage({type:'result',id,result});
  }catch(error){self.postMessage({type:'error',id,error:error.message})}
};
