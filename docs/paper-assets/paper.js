const themeButton=document.getElementById('theme');
function updateThemeLabel(){themeButton.textContent=document.documentElement.dataset.theme==='dark'?'Light mode':'Dark mode'}
themeButton.addEventListener('click',()=>{const value=document.documentElement.dataset.theme==='dark'?'light':'dark';document.documentElement.dataset.theme=value;try{localStorage.setItem('paper-theme',value)}catch(e){}updateThemeLabel()});
updateThemeLabel();
document.querySelectorAll('.copy-code').forEach(button=>button.addEventListener('click',async()=>{
  const text=button.closest('figure').querySelector('pre code').textContent;
  try{await navigator.clipboard.writeText(text);button.textContent='Copied';setTimeout(()=>button.textContent='Copy',1800)}catch(e){button.textContent='Select to copy';const range=document.createRange();range.selectNodeContents(button.closest('figure').querySelector('pre'));const selection=window.getSelection();selection.removeAllRanges();selection.addRange(range)}
}));
const dialog=document.getElementById('figure-dialog');
document.querySelectorAll('.image-zoom').forEach(button=>button.addEventListener('click',()=>{
  const original=button.querySelector('img');const image=document.getElementById('enlarged-figure');image.src=original.src;image.alt=original.alt;document.getElementById('figure-caption').textContent=button.closest('figure').querySelector('figcaption').textContent;dialog.showModal();
}));
document.getElementById('close-figure').addEventListener('click',()=>dialog.close());
dialog.addEventListener('click',event=>{if(event.target===dialog){const rect=dialog.getBoundingClientRect();if(event.clientX<rect.left||event.clientX>rect.right||event.clientY<rect.top||event.clientY>rect.bottom)dialog.close()}});
const tocLinks=Array.from(document.querySelectorAll('.toc-link'));
const observer=new IntersectionObserver(entries=>{entries.forEach(entry=>{if(entry.isIntersecting){tocLinks.forEach(link=>link.classList.toggle('active',link.hash==='#'+entry.target.id))}})},{rootMargin:'-10% 0px -75% 0px'});
document.querySelectorAll('article h2[id],article h3[id],#abstract').forEach(section=>observer.observe(section));
function progress(){const denominator=document.documentElement.scrollHeight-window.innerHeight;document.getElementById('progress').style.width=(denominator>0?Math.max(0,Math.min(100,window.scrollY/denominator*100)):0)+'%'}
window.addEventListener('scroll',progress,{passive:true});window.addEventListener('resize',progress);progress();
