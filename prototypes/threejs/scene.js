import * as THREE from '/vendor/three.module.js';

export function decodeBytes(text) {
  const binary=atob(text),bytes=new Uint8Array(binary.length);
  for(let i=0;i<binary.length;++i)bytes[i]=binary.charCodeAt(i);
  return bytes;
}
export function spriteQuad(sprite) {
  const x0 = sprite.x + (sprite.xd < 0 ? 1 : 0);
  const y0 = sprite.y + (sprite.yd < 0 ? 1 : 0);
  return [x0, y0, x0 + sprite.xd * sprite.width, y0 + sprite.yd * sprite.height];
}
const vertexShader = `varying vec2 texcoord;
void main(){texcoord=uv;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}`;
function quad(x0, y0, x1, y1) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute([x0,y0,0,x1,y0,0,x0,y1,0,x1,y1,0],3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute([0,0,1,0,0,1,1,1],2));
  g.setIndex([0,1,2,2,1,3]); return g;
}
function texture(data, width, height, format=THREE.RGBAFormat) {
  const t = new THREE.DataTexture(data,width,height,format);
  t.minFilter=t.magFilter=THREE.NearestFilter;
  t.needsUpdate=true; return t;
}
function material(fragmentShader, uniforms={}) {
  return new THREE.ShaderMaterial({vertexShader,fragmentShader,uniforms,side:THREE.DoubleSide,depthTest:false,depthWrite:false,blending:THREE.NoBlending,toneMapped:false});
}
export class SceneRenderer {
  constructor(canvas) {
    this.renderer=new THREE.WebGLRenderer({canvas,antialias:false});
    this.renderer.outputColorSpace=THREE.LinearSRGBColorSpace;
    this.renderer.setClearColor(0x000000,0);
    this.camera=new THREE.OrthographicCamera(0,320,0,224,0.1,100);
    this.camera.position.z=10;
    this.ground=new THREE.Scene();this.sprites=new THREE.Scene();this.composite=new THREE.Scene();
    const target=()=>new THREE.WebGLRenderTarget(320,224,{minFilter:THREE.NearestFilter,magFilter:THREE.NearestFilter,depthBuffer:false,stencilBuffer:false});
    this.groundTarget=target();this.spriteTarget=target();this.finalTarget=target();
    this.palette=texture(new Uint8Array(256*64*4),256,64);
    this.compositeMaterial=material(`
      uniform sampler2D ground,sprites,palette; varying vec2 texcoord;
      vec4 pen(float i){return texture2D(palette,(vec2(mod(i,256.0),floor(i/256.0))+0.5)/vec2(256.0,64.0));}
      void main(){
        vec2 uv=vec2(texcoord.x,1.0-texcoord.y);
        vec4 b=texture2D(ground,uv),s=texture2D(sprites,uv);
        float index=floor(b.r*255.0+0.5)+256.0*floor(b.g*255.0+0.5);
        float priority=floor(b.b*255.0+0.5);
        if(s.a>0.5){
          float word=floor(s.r*255.0+0.5)+256.0*floor(s.g*255.0+0.5);
          float sp=floor(mod(floor(word/4096.0),4.0));
          if(pow(2.0,sp)>priority){
            if(mod(floor(word/16384.0),2.0)>0.5 && mod(word,16.0)==10.0)index+=8192.0;
            else index=mod(word,4096.0);
          }
        }
        gl_FragColor=vec4(pen(index).rgb,1.0);
      }`,{ground:{value:this.groundTarget.texture},sprites:{value:this.spriteTarget.texture},palette:{value:this.palette}});
    this.composite.add(new THREE.Mesh(quad(0,0,320,224),this.compositeMaterial));
    this.present=new THREE.Scene();
    this.present.add(new THREE.Mesh(quad(0,0,320,224),material('uniform sampler2D frame; varying vec2 texcoord; void main(){gl_FragColor=texture2D(frame,vec2(texcoord.x,1.0-texcoord.y));}',{frame:{value:this.finalTarget.texture}})));
    this.spritePool=new Map();this.verify=false;
    this.outlines=new THREE.Scene();this.outlinePool=new Map();
    this.outlineMaterial=new THREE.MeshBasicMaterial({color:0x74f4c5,wireframe:true,depthTest:false,side:THREE.DoubleSide});
    const groundMaterial=material('varying vec3 value;void main(){gl_FragColor=vec4(value,1.0);}');
    groundMaterial.vertexShader='attribute vec3 encoded;varying vec3 value;void main(){value=encoded;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}';
    this.groundMesh=new THREE.Mesh(new THREE.BufferGeometry(),groundMaterial);
    this.groundMesh.frustumCulled=false;this.ground.add(this.groundMesh);
    this.groundCapacity=0;
    this.wireframe=false;this.lastStats={objects:0,strips:0,clips:0};
    this.renderer.setRenderTarget(this.finalTarget);this.renderer.setClearColor(0x000000,1);this.renderer.clear();
  }
  render(frame) {
    let objects=0,strips=0;
    if(frame.frame===0){
      this.renderer.setRenderTarget(this.finalTarget);this.renderer.setScissorTest(false);this.renderer.setClearColor(0x000000,1);this.renderer.clear();
      for(const mesh of this.spritePool.values())mesh.visible=false;
    }
    for(const clip of frame.clips) {
      const r=this.renderer;
      r.setRenderTarget(this.finalTarget);
      r.setScissor(0,223-clip.bottom,320,clip.bottom-clip.top+1);r.setScissorTest(true);
      if(!clip.display){r.setClearColor(0x000000,1);r.clear();r.setScissorTest(false);continue;}
      if(clip.palette!==undefined){this.palette.image.data=decodeBytes(clip.palette);this.palette.needsUpdate=true;}
      for(const mesh of this.spritePool.values())mesh.visible=false;
      const stripBytes=decodeBytes(clip.stripsLE);
      const stripData=new DataView(stripBytes.buffer,stripBytes.byteOffset,stripBytes.byteLength);
      const stripCount=stripBytes.length/10;
      const required=stripCount*18;
      if(required>this.groundCapacity){
        this.groundCapacity=Math.max(required,this.groundCapacity*2,18000);
        this.groundMesh.geometry.dispose();this.groundMesh.geometry=new THREE.BufferGeometry();
        this.groundMesh.geometry.setAttribute('position',new THREE.BufferAttribute(new Float32Array(this.groundCapacity),3).setUsage(THREE.DynamicDrawUsage));
        this.groundMesh.geometry.setAttribute('encoded',new THREE.BufferAttribute(new Float32Array(this.groundCapacity),3).setUsage(THREE.DynamicDrawUsage));
      }
      const g=this.groundMesh.geometry,positions=g.attributes.position.array,values=g.attributes.encoded.array;
      let offset=0;
      for(let i=0;i<stripBytes.length;i+=10){
        const x0=stripData.getUint16(i,true),x1=stripData.getUint16(i+2,true),y=stripData.getUint16(i+4,true),index=stripData.getUint16(i+6,true),priority=stripData.getUint16(i+8,true);
        const lo=(index&255)/255,hi=(index>>8)/255,pr=priority/255;
        for(let v=0;v<6;v++){
          positions[offset]=(v===1||v===4||v===5)?x1:x0;
          positions[offset+1]=(v===2||v===3||v===5)?y+1:y;positions[offset+2]=0;
          values[offset++]=lo;values[offset++]=hi;values[offset++]=pr;
        }
      }
      g.setDrawRange(0,required/3);
      for(const attribute of Object.values(g.attributes)){attribute.clearUpdateRanges();attribute.addUpdateRange(0,required);attribute.needsUpdate=true;}
      for(const s of clip.sprites) {
        let mesh=this.spritePool.get(s.slot);
        if(!mesh){
        if(s.pixels===undefined)throw new Error("Missing sprite snapshot; reload the page");
        const source=texture(new Uint8Array(s.sourceWidth*s.height),s.sourceWidth,s.height,THREE.RedFormat);
        const mat=material(`uniform sampler2D source;uniform vec2 sourceSize,outputSize;uniform float zoom,wordBase;varying vec2 texcoord;
          void main(){
            vec2 outputPixel=floor(texcoord*outputSize);
            vec2 sourcePixel=vec2(floor(outputPixel.x*zoom/512.0),outputPixel.y);
            float pixel=floor(texture2D(source,(sourcePixel+0.5)/sourceSize).r*255.0+0.5);
            if(pixel==0.0||pixel==15.0)discard;
            float word=wordBase+pixel;gl_FragColor=vec4(mod(word,256.0)/255.0,floor(word/256.0)/255.0,0.0,1.0);
          }`,{source:{value:source},sourceSize:{value:new THREE.Vector2(s.sourceWidth,s.height)},outputSize:{value:new THREE.Vector2(s.width,s.height)},zoom:{value:s.hzoom},wordBase:{value:(s.color<<4)|(s.priority<<12)}});
        mesh=new THREE.Mesh(quad(0,0,1,1),mat);mesh.renderOrder=s.slot;mesh.frustumCulled=false;
        this.sprites.add(mesh);this.spritePool.set(s.slot,mesh);
        }
        const uniforms=mesh.material.uniforms;
        let source=uniforms.source.value;
        if(s.pixels!==undefined){
        const pixels=decodeBytes(s.pixels);
        if(source.image.width!==s.sourceWidth||source.image.height!==s.height){
          source.dispose();source=texture(pixels,s.sourceWidth,s.height,THREE.RedFormat);uniforms.source.value=source;
        }else{source.image.data=pixels;source.needsUpdate=true;}
        }
        uniforms.sourceSize.value.set(s.sourceWidth,s.height);uniforms.outputSize.value.set(s.width,s.height);
        uniforms.zoom.value=s.hzoom;uniforms.wordBase.value=(s.color<<4)|(s.priority<<12);
        const [x0,y0,x1,y1]=spriteQuad(s);
        mesh.position.set(x0,y0,0);mesh.scale.set(x1-x0,y1-y0,1);mesh.visible=true;
      }
      objects+=clip.sprites.length;strips+=stripCount;
      r.setScissorTest(false);r.setClearColor(0x000000,0);r.setRenderTarget(this.groundTarget);r.clear();r.render(this.ground,this.camera);
      r.setRenderTarget(this.spriteTarget);r.clear();r.render(this.sprites,this.camera);
      r.setRenderTarget(this.finalTarget);r.setScissor(0,223-clip.bottom,320,clip.bottom-clip.top+1);r.setScissorTest(true);
      // autoClear would erase earlier partial clips. Clear only the native clip.
      r.render(this.composite,this.camera);r.setScissorTest(false);
    }
    let hash;
    if(this.verify&&frame.referenceHash!==undefined){
    const pixels=this.checkPixels??=new Uint8Array(320*224*4);this.renderer.readRenderTargetPixels(this.finalTarget,0,0,320,224,pixels);
    hash=2166136261;
    for(let y=223;y>=0;--y)for(let x=0;x<320;++x)for(let c=0;c<3;++c)hash=Math.imul(hash^pixels[(y*320+x)*4+c],16777619)>>>0;
    }
    this.lastStats={objects,strips,clips:frame.clips.length,matchesNative:hash===undefined?undefined:hash===frame.referenceHash,renderHash:hash};
  }
  presentFrame() {
    const r=this.renderer,canvas=r.domElement;
    const width=Math.round(canvas.clientWidth*devicePixelRatio),height=Math.round(canvas.clientHeight*devicePixelRatio);
    if(canvas.width!==width||canvas.height!==height)r.setSize(width,height,false);
    r.setRenderTarget(null);r.render(this.present,this.camera);
    if(this.wireframe){
      r.autoClear=false;
      for(const sprite of this.sprites.children){
        let outline=this.outlinePool.get(sprite.renderOrder);
        if(!outline){outline=new THREE.Mesh(sprite.geometry,this.outlineMaterial);outline.frustumCulled=false;this.outlines.add(outline);this.outlinePool.set(sprite.renderOrder,outline);}
        outline.position.copy(sprite.position);outline.scale.copy(sprite.scale);outline.visible=sprite.visible;
      }
      r.render(this.outlines,this.camera);r.autoClear=true;
    }
  }
}
