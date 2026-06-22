// import { truncate } from 'fs'
import { v4 } from 'uuid'
import { t } from '@/i18n'

function makeCirceNode(id, label) {
  return {
    id: id,
    shape: 'circle',
    width: 60,
    height: 60,
    attrs: {
      // body 是选择器名称，选中的是 rect 元素
      body: {
        fill: 'rgba(239,88,88,0.10)',
        stroke: 'rgba(239,88,88,1)',
        strokeWidth: 1,
      },
      label: {
        text: label,
        fontSize: 12,
        fontWeight: 'bold',
        fill: '#EF5858',
      },
    }
  }
}

function makeNode(id, label) {
  return {
    id: id,
    shape: 'rect',
    width: getTextWidth(label),
    height: 40,
    attrs: {
      // body 是选择器名称，选中的是 rect 元素
      body: {
        stroke: '#1890FF',
        strokeWidth: 1,
        fill: '#fff',
        rx: 2,
        ry: 2,
        cursor: 'pointer',
      },
      label: {
        // text: '\ue6aa' + ' ' + label,
        text: label,
        fontFamily: 'iconfont',
        fontSize: 12,
        fontWeight: 'bold',
        fill: '#1890FF',
        cursor: 'pointer',
        // textWrap: {
        //   name: truncate
        // }
      },
    }
  }
}

function makeEdge(source, target, onClickView) {
  return {
    shape: 'edge',
    source: source,
    target: target,
    attrs: {
      // line 是选择器名称，选中的边的 path 元素
      line: {
        stroke: '#C7CCDC',
        strokeWidth: 2,
        strokeDasharray: '2 3',
        targetMarker: 'block'
      }
    },
    router: 'orth',
    connector: {
      name: 'rounded',
      args: {
        radius: 5,
      },
    },
    tools: [
      {
        name: 'button',
        args: {
          markup: [
            {
              tagName: 'circle',
              selector: 'button',
              attrs: {
                r: 10,
                stroke: 'white',
                strokeWidth: 2,
                fill: 'white',
                cursor: 'pointer'
              }
            },
            {
              tagName: 'text',
              textContent: '\ue6aa',
              fontFamily: 'iconfont',
              selector: 'icon',
              attrs: {
                fontFamily: 'iconfont',
                fill: '#1890FF',
                fontSize: 12,
                textAnchor: 'middle',
                pointerEvents: 'none',
                y: '0.4em'
              }
            },
            // {
            //   tagName: 'image',  // Add the image element
            //   selector: 'backgroundImage',  // Add selector for the image
            //   attrs: {
            //     width: 20,  // Set the width of the image
            //     height: 20,  // Set the height of the image
            //     radius: 10,
            //     x: -10,  // Adjust the x-position to center the image
            //     y: -10,  // Adjust the y-position to center the image
            //     href: 'https://img2.baidu.com/it/u=140788575,4191915434&fm=253&fmt=auto&app=120&f=JPEG?w=500&h=500',  // Path to the image you want to use
            //   }
            // }
          ],
          distance: -40,
          onClick: function (view) {
            onClickView(view)
          }
          // onClick(view) {
          //   const edge = view.cell
          //   const source = edge.getSource()
          //   const target = edge.getTarget()
          //   const mouseX = view.e.clientX;
          //   const mouseY = view.e.clientY;
          //   console.log(source, target, '=====', mouseX, mouseY)
          // }
        }
      }
    ]
  }
}

function getTextWidth(text) {
  const canvas = document.createElement('canvas');
  const context = canvas.getContext('2d');
  const fontSize = 16;
  context.font = `${fontSize}px Arial`;
  const textWidth = context.measureText(text).width;
  // return textWidth < 100 ? 100 : textWidth + 30
  return 100
}

function generateActionId() {
  return v4().slice(0, 8)
}

function transformDataForElTree(originalData) {
  const transformedData = originalData.map(item => {
    const children = item.labelList.map(labelItem => {
      return {
        atomicCode: `${item.atomicCode}-${labelItem.label}`,
        atomicName: labelItem.nameCN,
        label: labelItem.nameCN,
        threshold: labelItem.threshold,
        class_name: labelItem.class_name,
        nameCN: labelItem.nameCN,
        used: labelItem.used
      };
    });

    return {
      atomicCode: item.atomicCode,
      atomicName: item.atomicName,
      label: item.atomicName,
      children
    };
  });

  return transformedData;
}



function getLogicalOperations() {
  return [
    { value: 1, label: t('glossary.logicOr') },
    { value: 2, label: t('glossary.logicAnd') },
    { value: 3, label: t('glossary.logicNot') }
  ]
}

function getOperationsMap() {
  return {
    'switch': [
      { value: 11, label: t('glossary.opEqual') }
    ],
    'select': [
      { value: 11, label: t('glossary.opEqual') },
      { value: 12, label: t('glossary.opNotEqual') },
      { value: 13, label: t('glossary.opGreater') },
      { value: 14, label: t('glossary.opGreaterEqual') },
      { value: 15, label: t('glossary.opLess') },
      { value: 16, label: t('glossary.opLessEqual') }
    ],
    select2: [
      { value: 11, label: t('glossary.opEqual') },
      { value: 12, label: t('glossary.opNotEqual') }
    ],
    'check': [
      { value: 31, label: t('glossary.opContains') },
      { value: 32, label: t('glossary.opNotContains') }
    ],
    'radio': [
      { value: 11, label: t('glossary.opEqual') }
    ],
    'other': [
      { value: 11, label: t('glossary.opEqual') },
      { value: 12, label: t('glossary.opNotEqual') },
      { value: 13, label: t('glossary.opGreater') },
      { value: 14, label: t('glossary.opGreaterEqual') },
      { value: 15, label: t('glossary.opLess') },
      { value: 16, label: t('glossary.opLessEqual') }
    ]
  }
}

function getOperations(type) {
  const ops = getOperationsMap()
  switch (type) {
    case 'switch':
      return ops.switch
    case 'select':
      return ops.select
    case 'select2':
      return ops.select2
    case 'check':
      return ops.check
    case 'radio':
      return ops.select
    default:
      return ops.other
  }
}

function setNodeHighlight(cell) {
  cell.attr('body/fill', '#1890FF')
  cell.attr('label/fill', 'white')
  cell.attr('body/filter', {
    name: 'outline',
    args: {
      color: '#1890FF',
      width: 3,
      margin: 0,
      opacity: 0.3
    }
  })
}

function setNodeNormal(cell) {
  cell.attr('body/fill', 'white')
  cell.attr('label/fill', '#1890FF')
  cell.attr('body/filter', {
    name: 'outline',
    args: {
      color: '#1890FF',
      width: 0,
      margin: 0,
      opacity: 0.5
    }
  })
}

export {
  makeCirceNode, makeNode, makeEdge, getTextWidth, generateActionId, setNodeHighlight, setNodeNormal,
  getLogicalOperations,
  getOperations
} 