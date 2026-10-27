/* SPDX-FileCopyrightText: 2026 Deskflow Voice contributors
 * SPDX-License-Identifier: MIT */
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const page = $('voice-page'), dialog = $('flow-dialog'), content = $('dialog-content');
  const icon = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><rect x="3" y="3" width="18" height="13" rx="2"/><path d="M8 21h8m-4-5v5"/></svg>';
  const micIcon = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><rect x="8" y="2" width="8" height="13" rx="4"/><path d="M5 10v2a7 7 0 0 0 14 0v-2m-7 9v3m-4 0h8"/></svg>';
  const defaults = platform => ({platform, role:platform === 'mac' ? 'send' : 'receive', enabled:true, configured:true, paired:true, source:'system', sourceVerified:true, key:platform === 'mac' ? '右 Option' : 'F8', startup:true, sender:'mac'});
  let config = defaults('mac'), baseline = {...config}, fault = '', guideReason = '', modal = null, wizard = null, quick = null, view = 'home', test = null, noticeTimer;
  const timers = new Set();
  const later = (fn, ms) => {const id=setTimeout(() => {timers.delete(id);fn();},ms);timers.add(id);return id;};
  const clearTimers = () => {timers.forEach(clearTimeout);timers.clear();};
  const esc = value => String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const button = (label, action, primary=false, disabled=false) => `<button data-action="${action}" ${primary?'class="primary"':''} ${disabled?'disabled':''}>${label}</button>`;
  const sourceLabel = () => config.source === 'siri' ? 'Siri 遥控器' : config.source === 'usb' ? 'USB 麦克风' : config.platform === 'mac' ? 'Mac 系统麦克风' : 'Windows 系统麦克风';
  const ownName = () => config.platform === 'mac' ? 'Mac 笔记本' : config.role === 'send' ? 'Windows 办公' : 'Windows 书房';
  const senderName = () => config.sender === 'windows' ? 'Windows 办公' : 'Mac 笔记本';
  const device = name => `<span class="device-mini"><span class="device-icon">${icon}</span>${esc(name)}</span>`;
  const status = (text, bad=false) => `<span class="status-text ${bad?'warn':''}">${text}</span>`;
  const dirty = () => JSON.stringify(config) !== JSON.stringify(baseline);
  function toast(message){clearTimeout(noticeTimer);$('toast').textContent=message;$('toast').classList.add('visible');noticeTimer=setTimeout(()=>$('toast').classList.remove('visible'),2800);}
  function issue(){
    if(!config.enabled)return {title:'跨设备语音已关闭',detail:'开启后，按住说话即可把声音发送到另一台 Windows。',kind:'off'};
    if(!config.configured||!config.paired)return {title:'完成一次设置，即可开始使用',detail:'选择设备与声音来源，检查组件，再完成一次输入测试。',kind:'off',action:'setup',label:'开始设置'};
    if(fault==='offline')return {title:'等待对端上线',detail:'对端上线并连接 Deskflow 后，再按下说话键即可。',kind:'warning',action:'repair',label:'检查连接'};
    if(fault==='mic')return {title:config.role==='send'?'声音来源不可用':'发送端声音来源不可用',detail:'重新连接设备，或在发送端选择另一个麦克风。',kind:'warning',action:'repair',label:'检查声音'};
    if(fault==='cable')return {title:'Windows 接收音频组件尚未就绪',detail:'完成接收端组件安装并重新检测，才能开始语音输入。',kind:'warning',action:'repair',label:'查看解决方法'};
    if(fault==='ime')return {title:'当前豆包版本尚未通过兼容验证',detail:'暂时无法启用跨设备语音，请查看支持的版本与处理方式。',kind:'warning',action:'repair',label:'查看兼容说明'};
    if(config.role==='send'&&!config.sourceVerified)return {title:'检查新的声音来源',detail:'确认新声源的设备连接、权限和组件，再开始测试。',kind:'warning',action:'components',label:'检查来源'};
    return {title:'可使用',detail:config.role==='send'?'当前控制 Windows 书房。按住说话，文字进入它的当前输入位置。':`已允许 ${senderName()} 发送声音，文字输入到本机当前输入位置。`,kind:''};
  }
  function render(){
    const i=issue(), send=config.role==='send';
    const ready=config.enabled&&config.configured&&config.paired&&!fault&&(!send||config.sourceVerified);
    document.querySelector('.app-window').classList.toggle('windows',config.platform==='windows');
    $('platform-label').textContent=config.platform==='mac'?'macOS':'Windows';
    const heading=`<header class="feature-header"><h1>跨设备语音</h1><label class="switch-label"><input id="feature-enabled" class="switch" type="checkbox" role="switch" ${config.enabled?'checked':''}>${config.enabled?'已开启':'已关闭'}</label></header>`;
    const tabs=`<nav class="voice-tabs" aria-label="语音页面">${[['home','使用'],['devices','设备'],['settings','设置']].map(([key,label])=>`<button data-action="page-${key}" class="${view===key?'active':''}" aria-current="${view===key?'page':'false'}">${label}</button>`).join('')}</nav>`;
    const route=`<div class="pair-line">${device(send?ownName():senderName())}<span class="route-arrow" aria-hidden="true"></span>${device(send?'Windows 书房':ownName())}</div>`;
    const row=(label,body,action='')=>`<div class="group-row"><div class="row-label">${label}</div><div class="row-body">${body}</div>${action?`<div class="row-actions">${action}</div>`:''}</div>`;
    let body='';
    if(view==='home'){
      const title=ready?'已就绪':!config.configured?'说话，就在另一台电脑输入':!config.enabled?'开启跨设备语音':i.title;
      const hint=ready?(send?`按住 <kbd>${config.source==='siri'?'遥控器语音键':esc(config.key)}</kbd> 说话，松开结束。`:`在 ${senderName()} 按住说话，这里直接输入。`):!config.configured?(send?'默认使用本机麦克风。':'自动检查接收端组件。'):i.detail;
      const action=!config.enabled?'enable-service':!config.configured||!config.paired?'quick-start':fault?'repair':!ready?'components':'test';
      const label=!config.enabled?'开启语音':!config.configured||!config.paired?'开始使用':fault?'解决问题':!ready?'检查来源':'试一下';
      body=`<section class="home-stage ${fault?'has-issue':''}" aria-label="语音使用"><div class="home-route">${route}</div><div class="home-mic ${ready?'ready':''}" aria-hidden="true">${micIcon}</div><h2 role="status">${title}</h2><p class="home-hint">${hint}</p><div class="home-action">${button(label,action,true)}</div><button class="text-button" data-action="page-devices">查看设备</button></section>`;
    }
    if(view==='devices'){
      const roles=`<div class="segmented" aria-label="本机用途"><button data-action="role-send" class="${send?'active':''}" aria-pressed="${send}">发送声音</button><button data-action="role-receive" class="${!send?'active':''}" aria-pressed="${!send}" ${config.platform==='mac'?'disabled title="首版仅支持 Windows 接收语音"':''}>接收语音</button></div>`;
      body=`<div class="group">${row('本机用途',roles)}${row(send?'发送到':'接受来自',`${route}<span class="compact-note">${config.paired?(fault==='offline'?'离线':'已配对'):'未配对'}</span>`,button('管理','pair'))}</div>`;
      body+=send?`<div class="group">${row('声音来源',`<strong>${sourceLabel()}</strong>${!config.sourceVerified?'<span class="compact-note">需要检查</span>':''}`,button('更换','source'))}${config.source==='siri'?row('遥控器组件',status(fault==='mic'||!config.sourceVerified?'需要检查':'已就绪',fault==='mic'||!config.sourceVerified),button('查看','components')):''}</div>`:`<div class="group">${row('语音输入','豆包输入法',status(fault==='ime'?'待兼容':'已就绪',fault==='ime'))}${row('接收音频','跨设备语音音频',status(fault==='cable'?'缺少组件':'已就绪',fault==='cable'))}${row('输入位置','当前输入框')}</div>`;
      if(!config.paired||fault||!config.sourceVerified&&send)body+=`<div class="inline-problem"><span>${i.title}</span>${button('处理','repair')}</div>`;
      body+=`<button class="text-button" data-action="components">检查设备与组件</button>`;
    }
    if(view==='settings'){
      body=`<div class="group">${send?row('按住说话',`<kbd>${esc(config.key)}</kbd>`,button('更改','shortcut')):''}${row('自动准备',`<label class="check-label"><input id="login-startup" type="checkbox" ${config.startup?'checked':''}>登录后自动启动</label>`)}</div><details class="advanced-options"><summary>更多选项</summary><div class="advanced-links">${button('设备与组件','components')}${button('完整设置向导','setup')}${button('诊断信息','diagnostics')}</div></details>`;
    }
    page.innerHTML=`<div class="page page-${view}">${heading}${tabs}${body}</div>`;
    $('save-settings').disabled=!dirty();$('cancel-settings').disabled=!dirty();$('save-note').textContent=dirty()?'更改未保存':'';
    $('settings-actions').hidden=view==='home'&&!dirty();
    document.querySelector('.app-footer').classList.toggle('compact-footer',view==='home'&&!dirty());
  }
  function shell(title, subtitle, body, footer='', steps=''){
    content.innerHTML=`<header class="dialog-header"><div><h2 id="dialog-title">${title}</h2><p>${subtitle}</p></div><button class="close-button" data-action="close-dialog" aria-label="关闭">×</button></header>${steps}<div class="dialog-body">${body}</div>${footer?`<footer class="dialog-footer">${footer}</footer>`:''}`;
  }
  function open(kind){clearTimers();modal=kind;test=null;if(!dialog.open)dialog.showModal();drawModal();}
  function close(){
    clearTimers();
    if(modal==='test'&&test?.fromQuick){test=null;modal='quick';drawQuick();return;}
    if(modal==='test'&&test?.fromWizard){test=null;modal='wizard';drawWizard();return;}
    if(quick&&!quick.completed){config={...quick.previous};fault=quick.previousFault;}
    if(wizard&&!wizard.completed){config={...wizard.previous};fault=wizard.previousFault;}
    test=null;modal=null;wizard=null;quick=null;dialog.close();render();
  }
  function checks(items){return `<ul class="check-list">${items.map(([label,ok,action])=>`<li><span><i class="check-icon ${ok?'ok':'bad'}">${ok?'✓':'!'}</i>${label}</span>${action?button(ok?'查看':'处理',action):status(ok?'已就绪':'待处理',!ok)}</li>`).join('')}</ul>`;}
  function drawModal(){
    if(modal==='quick'){drawQuick();return;}
    if(modal==='wizard'){drawWizard();return;}
    if(modal==='test'){drawTest();return;}
    if(modal==='source'){
      shell('选择声音来源','声音只在按住说话期间发送。',`<div class="form-row"><label for="source-choice">声音来源</label><select id="source-choice">${[['system',config.platform==='mac'?'Mac 系统麦克风':'Windows 系统麦克风'],['usb','USB 麦克风'],...(config.platform==='mac'?[['siri','Siri 遥控器']]:[])].map(([v,n])=>`<option value="${v}" ${config.source===v?'selected':''}>${n}</option>`).join('')}</select></div><p class="note">普通麦克风从系统设备中选择。蓝牙配对由系统完成；Siri 遥控器另有专用收音组件检查。</p>`,`${button('取消','close-dialog')}<div class="right">${button('使用此来源','use-source',true)}</div>`);return;
    }
    if(modal==='shortcut'){
      shell('设置按住说话键','选择一个便于按住、且不与本机输入法冲突的按键。',`<p>当前：<kbd>${esc(config.key)}</kbd></p><div class="shortcut-options">${(config.platform==='mac'?['右 Option','F8','Control + 空格']:['F8','右 Ctrl','Control + 空格']).map(k=>`<button data-action="choose-key" data-key="${k}" class="${config.key===k?'selected':''}">${k}</button>`).join('')}</div><p class="note">Siri 遥控器使用设备上的语音键。键盘快捷键和遥控器都遵循按住开始、松开停止。</p><p class="tiny-label">原型使用候选按键选择；正式界面提供录键和冲突检查。</p>`,`${button('完成','close-dialog',true)}`);return;
    }
    if(modal==='pair'){
      shell('管理语音设备','语音设备身份独立确认；一次会话只使用一个方向。',`${config.role==='receive'?`<div class="form-row"><label for="sender-choice">声音发送端</label><select id="sender-choice"><option value="mac" ${config.sender==='mac'?'selected':''}>Mac 笔记本</option><option value="windows" ${config.sender==='windows'?'selected':''}>Windows 办公</option></select></div>`:''}<div class="pair-line">${device(config.role==='send'?ownName():senderName())}<span class="route-arrow"></span>${device(config.role==='send'?'Windows 书房':ownName())}</div>${checks([['Deskflow 键鼠连接',fault!=='offline'],['语音设备身份',config.paired]])}${!config.paired?'<div class="pair-code"><div><h3>核对两端的确认码</h3><small>两边一致后再确认。此处为演示码。</small></div><strong>482 619</strong></div>':''}`,`${button(config.paired?'解除语音配对':'确认此设备',config.paired?'unpair':'confirm-pair')}<div class="right">${button('完成','close-dialog',true)}</div>`);return;
    }
    if(modal==='diagnostics'){
      shell('诊断与帮助','查看状态，定位声音、连接或输入法的问题。',`<div class="dialog-section"><h3>当前演示状态</h3><pre class="diagnostic">本机：${ownName()}\n用途：${config.role==='send'?'发送声音':'接收语音'}\n设备身份：${config.paired?'已确认':'未确认'}\n声音来源：${config.role==='send'?sourceLabel():'由发送端选择'}\n状态：${issue().title}\n会话：空闲，未收音\n诊断不包含录音、识别文字或凭据。</pre></div><div class="dialog-section"><h3>常见问题</h3><p class="small">没有声音：检查发送端来源与权限。<br>声音已到但没有语音条：检查接收端豆包兼容状态。<br>语音条出现但没有字：检查豆包录音设备和当前输入位置。</p></div>`,`${button('检查与修复','repair')}<div class="right">${button('关闭','close-dialog',true)}</div>`);return;
    }
    if(modal==='guide'){
      const reason=guideReason||fault;
      const text=reason==='ime'?'<ol class="instruction-list"><li>查看当前输入法版本是否在已验证的兼容范围。</li><li>未知版本暂停语音能力，保留 Deskflow 键鼠功能。</li><li>等待适配更新，或按正式产品提供的官方兼容方案处理；不静默关闭输入法更新。</li></ol><p class="note warning-note">此原型不指定或下载豆包版本，不把“重新检测”当作版本已兼容的证明。</p>':reason==='offline'?'<ol class="instruction-list"><li>确认对端已启动 Deskflow 并连接同一可达网络。</li><li>先恢复鼠标跨屏，再检查语音设备连接。</li><li>连接恢复后，需要一次新的按下才会开始；旧声音不会重放。</li></ol>':reason==='mic'?'<ol class="instruction-list"><li>确认麦克风或遥控器已连接发送电脑。</li><li>在系统设置中检查麦克风权限。</li><li>Siri 遥控器还需检查专用收音组件，或改用普通麦克风。</li></ol>':'<ol class="instruction-list"><li>在 Windows 接收端打开音频组件的官方安装指引。</li><li>按官方安装程序完成安装及必要的系统授权。</li><li>把豆包的录音设备设为跨设备语音接收音频，保留系统默认麦克风。</li><li>返回这里重新检测，再完成一次真实输入测试。</li></ol>';
      shell(reason==='ime'?'输入法兼容说明':reason==='offline'?'恢复设备连接':reason==='mic'?'恢复声音来源':'接收音频安装指引','按具体问题处理，不要求手填脚本或系统路径。',text,`${button('返回检查','repair')}<div class="right">${reason==='ime'||reason!==fault?button('关闭','close-dialog',true):button('模拟处理完成并重新检测','simulate-repair',true)}</div>`);return;
    }
    const siri=config.platform==='mac'&&config.role==='send'&&config.source==='siri';
    shell(modal==='repair'?'检查与修复':'设备与组件设置','逐项检查，缺少哪一项就处理哪一项。',`${checks([['两台电脑已连接',fault!=='offline','guide-offline'],['发送端声音来源',fault!=='mic'&&(config.role!=='send'||config.sourceVerified),'guide-mic'],['Windows 豆包输入法',fault!=='ime','guide-ime'],['Windows 接收音频组件',fault!=='cable','guide-cable']])}${siri?`<div class="dialog-section"><h3>Siri 遥控器增强组件</h3>${checks([['遥控器已在 Mac 系统配对',fault!=='mic'&&config.sourceVerified],['专用收音组件',fault!=='mic'&&config.sourceVerified],['PacketLogger 检查',fault!=='mic'&&config.sourceVerified]])}<p class="small">普通麦克风无需这些增强组件。缺少它们只影响 Siri 声源。</p></div>`:''}<p class="note">设备与检测状态为原型演示。正式产品按权限和官方安装流程处理第三方组件。</p>`,`${config.role==='send'?button('选择声音来源','source'):button('管理设备','pair')}<div class="right">${config.role==='send'&&!config.sourceVerified?button('模拟来源检查通过','verify-source',true):button('完成','close-dialog',true)}</div>`);
  }
  function quickStart(){
    quick={previous:{...config},previousFault:fault,completed:false};
    open('quick');
  }
  function drawQuick(){
    const send=config.role==='send';let body='',footer='';
    if(!config.paired){
      body=`<div class="quick-intro"><span class="device-icon">${icon}</span><h3>连接 ${send?'Windows 书房':senderName()}</h3></div><div class="pair-code"><span>确认两端代码一致</span><strong>482 619</strong></div>`;
      footer=`${button('稍后','close-dialog')}<div class="right">${button('确认并继续','quick-pair',true)}</div>`;
    }else if(fault){
      body=`<div class="quick-intro"><h3>${issue().title}</h3><p>${issue().detail}</p></div>`;
      footer=`${button('稍后','close-dialog')}<div class="right">${button('查看解决方法','quick-repair',true)}</div>`;
    }else if(send&&!config.sourceVerified){
      body=`<div class="quick-intro"><span class="device-icon">${micIcon}</span><h3>${config.source==='siri'?'检查 Siri 声源':'允许使用麦克风'}</h3><p>${config.source==='siri'?'需要确认遥控器配对及专用收音组件。':'使用 '+sourceLabel()+'。'}</p></div>`;
      footer=`${button('稍后','close-dialog')}<div class="right">${button(config.source==='siri'?'检查并继续':'允许并继续','quick-prepare',true)}</div>`;
    }else{
      body=`<div class="quick-intro"><span class="status-icon">✓</span><h3>准备好了，试说一句</h3><p>最后确认出字和松开停止。</p></div>`;
      footer=`${button('稍后','close-dialog')}<div class="right">${button('试一下','quick-test',true)}</div>`;
    }
    shell('开始使用','仅在需要时补齐这一步。所有结果为演示。',body,footer);
  }
  function setup(){wizard={step:0,paired:config.paired,prepared:config.configured&&!fault&&(config.role==='receive'||config.sourceVerified),tested:false,previous:{...config},previousFault:fault};open('wizard');}
  function wizardSteps(){return `<ol class="steps" aria-label="设置进度">${['选择用途','连接设备','准备声音','按键与启动','测试完成'].map((s,i)=>`<li class="${i===wizard.step?'current':i<wizard.step?'done':''}" ${i===wizard.step?'aria-current="step"':''}><b>${i<wizard.step?'✓':i+1}</b>${s}</li>`).join('')}</ol>`;}
  function drawWizard(){
    const step=wizard.step,send=config.role==='send';let body='';
    if(step===0)body=`<div class="intro"><h3>这台电脑做什么？</h3><p>按声音的方向选择，与 Deskflow 的控制端、被控端分开设置。</p></div><label class="choice ${send?'selected':''}"><input type="radio" name="wizard-role" value="send" ${send?'checked':''}><span><strong>声音从这台电脑发出</strong><p>使用本机麦克风，把文字输入到另一台 Windows。</p></span></label><label class="choice ${!send?'selected':''} ${config.platform==='mac'?'disabled':''}"><input type="radio" name="wizard-role" value="receive" ${!send?'checked':''} ${config.platform==='mac'?'disabled':''}><span><strong>文字输入到这台电脑</strong><p>${config.platform==='mac'?'Mac 接收将在后续版本提供。':'接收另一台 Mac 或 Windows 的声音。'}</p></span></label>`;
    if(step===1)body=`<div class="intro"><h3>确认另一台电脑</h3><p>先接通 Deskflow 键鼠连接，再确认语音设备身份。</p></div><div class="choice selected"><span class="device-icon">${icon}</span><div><strong>${send?'Windows 书房':senderName()}</strong><p>已通过 Deskflow 发现（演示）</p></div></div><div class="pair-code"><div><h3>核对两端的确认码</h3><small>演示码，不是真实配对凭据。</small></div><strong>482 619</strong></div>${wizard.paired?status('设备身份已确认'):button('两端一致，确认此设备','wizard-pair',true)}`;
    if(step===2)body=`<div class="intro"><h3>${send?'准备声音来源':'准备本机接收'}</h3><p>${send?'选择本机已有麦克风，或设置 Mac 的 Siri 遥控器。':'检查豆包输入法及接收音频组件。'}</p></div>${send?`<div class="form-row"><label for="wizard-source">声音来源</label><select id="wizard-source">${[['system',config.platform==='mac'?'Mac 系统麦克风':'Windows 系统麦克风'],['usb','USB 麦克风'],...(config.platform==='mac'?[['siri','Siri 遥控器']]:[])].map(([v,n])=>`<option value="${v}" ${config.source===v?'selected':''}>${n}</option>`).join('')}</select></div>`:''}${checks(send?[['本机麦克风权限',wizard.prepared],...(config.source==='siri'?[['遥控器系统配对',wizard.prepared],['Siri 收音组件及 PacketLogger',wizard.prepared]]:[]),['对端输入法与音频组件',wizard.prepared]]:[['豆包输入法及兼容版本',wizard.prepared],['接收音频组件',wizard.prepared],['豆包的录音设备',wizard.prepared]])}${wizard.prepared?'<p class="small">准备检查已通过（演示）。接下来设置按住说话方式。</p>':`${button(wizard.showPreparation?'收起准备步骤':'查看准备步骤','wizard-preparation')}${button(fault==='ime'?'当前版本待兼容':'模拟准备完成并检测','wizard-prepare',true,fault==='ime')}`}${wizard.showPreparation?'<ol class="instruction-list"><li>发送端：确认声音设备已连接，在系统设置中允许麦克风访问。</li><li>Siri 声源另需完成系统蓝牙配对、专用收音组件及 PacketLogger 检查。</li><li>Windows 接收端：按官方流程安装受支持的豆包输入法与接收音频组件，再选择豆包录音设备。</li><li>未知输入法版本需要先完成兼容处理，不能直接当作已就绪。</li></ol>':''}<p class="note">蓝牙配对、系统权限和第三方组件安装各自确认。不会静默改变系统默认麦克风。</p>`;
    if(step===3)body=`<div class="intro"><h3>${send?'按住开始，松开停止':'让接收端随时就绪'}</h3><p>${send?'选择不与本机输入法冲突的按键。遥控器继续使用语音键。':'按键在发送端设置；本机跟随每次新按下的输入位置。'}</p></div>${send?`<div class="form-row"><label for="wizard-key">按住说话键</label><select id="wizard-key">${(config.platform==='mac'?['右 Option','F8','Control + 空格']:['F8','右 Ctrl','Control + 空格']).map(k=>`<option ${config.key===k?'selected':''}>${k}</option>`).join('')}</select></div>`:''}<label class="choice"><input id="wizard-startup" type="checkbox" ${config.startup?'checked':''}><span><strong>登录后自动准备语音</strong><p>等待网络、设备与输入法就绪，不需要每次手动启动。</p></span></label><p class="note">按住时切换窗口、电脑或断线，会结束本轮。恢复后再按一次即可。</p>`;
    if(step===4)body=`<div class="intro"><h3>最后，试一次输入</h3><p>分别确认声音、到达、语音启动、出字和松开停止。</p></div>${wizard.tested?`${checks([['声音来源',true],['声音到达接收端',true],['语音启动、出字和松开停止',true]])}<p class="note">演示流程已完成。正式产品须由用户确认真实输入结果。</p>`:`<div class="test-console"><span class="device-icon">${micIcon}</span><p>先走一遍测试，再完成设置。</p>${button('开始演示测试','wizard-test',true)}</div>`}`;
    const allowed=step!==1||wizard.paired;
    const nextDisabled=!allowed||(step===2&&!wizard.prepared)||(step===4&&!wizard.tested);
    shell('设置跨设备语音',`${config.platform==='mac'?'Mac':'Windows'} · ${send?'声音发送端':'语音接收端'}`,body,`${step>0?button('上一步','wizard-back'):button('取消','close-dialog')}<div class="right">${button(step===4?'完成设置':'下一步',step===4?'wizard-finish':'wizard-next',true,nextDisabled)}</div>`,wizardSteps());
  }
  function startTest(fromWizard=false,fromQuick=false){test={stage:'idle',rounds:0,text:'',fromWizard,fromQuick,held:false,confirmed:false};modal='test';if(!dialog.open)dialog.showModal();drawTest();}
  function drawTest(){
    const done=test.stage==='done',held=test.held,passed=held?3:done?4:0;
    const labels=['声音来源','声音到达','语音启动','出字与停止'];
    const message=held?'正在接收声音 · 松开后停止':done?'录音已停止，演示文字已进入测试框':'按住说话，松开停止。';
    shell('测试跨设备语音','演示模式，不访问麦克风。',`<details class="test-details"><summary>查看检测过程</summary><div class="test-checks">${labels.map((n,i)=>`<div class="test-step ${i<passed?'done':''}"><i class="check-icon ${i<passed?'ok':''}">${i<passed?'✓':i+1}</i>${n}</div>`).join('')}</div></details><div class="test-console"><div class="test-status" id="test-status" role="status">${message}</div><div class="level test-level ${held?'active':''}"><span></span></div><button class="hold-button ${held?'holding':'primary'}" id="hold-to-talk" aria-label="按住演示说话">${held?'正在演示说话，松开停止':'按住演示说话'}</button><p class="small">按住按钮，或按住空格。</p><textarea aria-label="演示输入结果" readonly placeholder="演示文字会出现在这里">${esc(test.text)}</textarea></div>${done?`<p class="small test-round">已完成 ${test.rounds} 轮演示，可继续试说。</p>`:''}`,`${button('重新开始','test-reset')}<div class="right">${button(done?'已出字，完成':'关闭',done?'test-confirm':'close-dialog',done)}</div>`);
  }
  function holdStart(){
    if(!test||test.held||test.stage==='finishing')return;
    test.held=true;test.stage='holding';
    const confirm=content.querySelector('[data-action="test-confirm"]');
    if(confirm){confirm.disabled=true;confirm.textContent='松开后再确认';}
    const hold=$('hold-to-talk');hold.classList.add('holding');hold.textContent='正在演示说话，松开停止';hold.classList.remove('primary');
    content.querySelector('.test-level').classList.add('active');$('test-status').textContent='正在接收声音 · 松开后停止';
    content.querySelectorAll('.test-step').forEach((el,i)=>{el.classList.toggle('done',i<3);el.querySelector('i').classList.toggle('ok',i<3);el.querySelector('i').textContent=i<3?'✓':String(i+1);});
  }
  function holdEnd(){
    if(!test||!test.held)return;
    test.held=false;test.stage='finishing';const hold=$('hold-to-talk');if(hold){hold.classList.remove('holding');hold.textContent='已松开，正在结束';hold.disabled=true;content.querySelector('.test-level').classList.remove('active');$('test-status').textContent='录音已停止，等待演示文字';}
    later(()=>{if(!test)return;test.rounds++;test.text+=(test.text?'\n':'')+(test.rounds===1?'跨设备语音测试成功':`第 ${test.rounds} 句连续输入成功`);test.stage='done';drawTest();},450);
  }
  function chooseRole(role){if(role==='receive'&&config.platform==='mac')return;config.role=role;render();}
  document.addEventListener('click',event=>{
    const element=event.target.closest('[data-action]');if(!element||element.disabled)return;
    const action=element.dataset.action;
    if(action==='close-dialog'){close();return;}
    if(action.startsWith('page-')){view=action.slice(5);render();return;}
    if(action==='enable-service'){config.enabled=true;baseline.enabled=true;if(!config.configured)quickStart();render();return;}
    if(action==='quick-start'){quickStart();return;}
    if(action==='quick-pair'){config.paired=true;drawQuick();return;}
    if(action==='quick-prepare'){config.sourceVerified=true;drawQuick();return;}
    if(action==='quick-test'){startTest(false,true);return;}
    if(action==='quick-repair'){guideReason=fault;modal='guide';drawModal();return;}
    if(action==='role-send'||action==='role-receive'){chooseRole(action==='role-send'?'send':'receive');return;}
    if(action==='save'){baseline={...config};render();toast('演示设置已保存');return;}
    if(action==='cancel'){config={...baseline};render();return;}
    if(action==='setup'){setup();return;}
    if(action==='test'){startTest();return;}
    if(action==='repair'&&quick){modal='quick';drawQuick();return;}
    if(['repair','components','source','pair','diagnostics','shortcut'].includes(action)){open(action);return;}
    if(action==='use-source'){const value=$('source-choice').value;if(config.source!==value){config.source=value;config.sourceVerified=false;}close();return;}
    if(action==='verify-source'){config.sourceVerified=true;drawModal();render();return;}
    if(action==='choose-key'){config.key=element.dataset.key;drawModal();render();return;}
    if(action==='unpair'){config.paired=false;drawModal();render();return;}
    if(action==='confirm-pair'){config.paired=true;drawModal();render();return;}
    if(action.startsWith('guide-')){guideReason=action.slice(6);open('guide');return;}
    if(action==='guide'){guideReason=fault;open('guide');return;}
    if(action==='simulate-repair'){element.disabled=true;element.textContent='正在演示检测…';later(()=>{fault='';$('preview-scene').value='ready';if(quick){modal='quick';drawQuick();}else open('components');render();toast('演示检测完成');},650);return;}
    if(action==='wizard-pair'){wizard.paired=true;drawWizard();return;}
    if(action==='wizard-prepare'){element.disabled=true;element.textContent='正在演示检测…';later(()=>{wizard.prepared=true;config.sourceVerified=true;fault='';drawWizard();},650);return;}
    if(action==='wizard-preparation'){wizard.showPreparation=!wizard.showPreparation;drawWizard();return;}
    if(action==='wizard-next'){wizard.step=Math.min(4,wizard.step+1);drawWizard();return;}
    if(action==='wizard-back'){wizard.step=Math.max(0,wizard.step-1);drawWizard();return;}
    if(action==='wizard-test'){startTest(true);return;}
    if(action==='wizard-finish'){config.configured=true;config.enabled=true;config.paired=wizard.paired;wizard.completed=true;baseline={...config};$('preview-scene').value='ready';close();toast('设置演示完成，已回到语音主页面');return;}
    if(action==='test-reset'){const fromWizard=test.fromWizard,fromQuick=test.fromQuick;clearTimers();startTest(fromWizard,fromQuick);return;}
    if(action==='test-confirm'){if(test?.stage!=='done')return;const fromWizard=test.fromWizard,fromQuick=test.fromQuick;test=null;if(fromQuick){quick.completed=true;config.configured=true;config.enabled=true;baseline={...config};$('preview-scene').value='ready';close();toast('已准备就绪');}else if(fromWizard){wizard.tested=true;modal='wizard';drawWizard();}else{close();toast('演示测试已确认');}return;}
  });
  document.addEventListener('change',event=>{
    const el=event.target;
    if(el.id==='preview-platform'){clearTimers();test=null;wizard=null;quick=null;modal=null;view='home';if(dialog.open)dialog.close();config=defaults(el.value);baseline={...config};fault='';$('preview-scene').value='ready';render();}
    if(el.id==='preview-scene'){clearTimers();test=null;wizard=null;quick=null;modal=null;view='home';if(dialog.open)dialog.close();fault=el.value==='ready'||el.value==='first'?'':el.value;config.configured=el.value!=='first';config.paired=el.value!=='first';config.sourceVerified=el.value!=='first';if(el.value==='first'){config.source='system';config.sourceVerified=false;}config.enabled=true;baseline={...config};render();}
    if(el.id==='feature-enabled'){config.enabled=el.checked;baseline.enabled=el.checked;if(config.enabled&&!config.configured)quickStart();render();}
    if(el.id==='login-startup'){config.startup=el.checked;render();}
    if(el.id==='sender-choice'){config.sender=el.value;drawModal();render();}
    if(el.name==='wizard-role'){config.role=el.value;wizard.tested=false;drawWizard();}
    if(el.id==='wizard-source'){config.source=el.value;config.sourceVerified=false;wizard.prepared=false;wizard.tested=false;drawWizard();}
    if(el.id==='wizard-key')config.key=el.value;
    if(el.id==='wizard-startup')config.startup=el.checked;
  });
  document.addEventListener('pointerdown',event=>{const b=event.target.closest('#hold-to-talk');if(!b||event.button!==0)return;event.preventDefault();b.focus();b.setPointerCapture?.(event.pointerId);holdStart();});
  document.addEventListener('pointerup',holdEnd);
  document.addEventListener('pointercancel',holdEnd);
  document.addEventListener('lostpointercapture',holdEnd);
  document.addEventListener('keydown',event=>{if(event.target.id==='hold-to-talk'&&(event.key===' '||event.key==='Enter')){event.preventDefault();if(!event.repeat)holdStart();}});
  document.addEventListener('keyup',event=>{if((event.key===' '||event.key==='Enter')&&test?.held){event.preventDefault();holdEnd();}});
  window.addEventListener('blur',holdEnd);
  document.addEventListener('visibilitychange',()=>{if(document.hidden)holdEnd();});
  dialog.addEventListener('cancel',event=>{event.preventDefault();close();});
  render();
})();
